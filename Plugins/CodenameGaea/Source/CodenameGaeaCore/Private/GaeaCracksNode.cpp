#include "GaeaCracksNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor CracksTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor CracksNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor CracksIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor CracksNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	uint32 CracksHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352dU;
		Value ^= Value >> 15;
		Value *= 0x846ca68bU;
		Value ^= Value >> 16;
		return Value;
	}

	float CracksHash01(int32 X, int32 Y, int32 Seed, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		H ^= Salt;
		return static_cast<float>(CracksHash(H) & 0x00ffffffU) / static_cast<float>(0x01000000U);
	}

	float CracksSmoothNoise(float X, float Y, int32 Seed, uint32 Salt)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float FX = X - static_cast<float>(X0);
		const float FY = Y - static_cast<float>(Y0);
		const float SX = FX * FX * (3.0f - 2.0f * FX);
		const float SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(CracksHash01(X0, Y0, Seed, Salt), CracksHash01(X0 + 1, Y0, Seed, Salt), SX);
		const float B = FMath::Lerp(CracksHash01(X0, Y0 + 1, Seed, Salt), CracksHash01(X0 + 1, Y0 + 1, Seed, Salt), SX);
		return FMath::Lerp(A, B, SY);
	}

	float CracksVoronoiEdge(float X, float Y, float Jitter, int32 Seed)
	{
		const int32 BaseX = FMath::FloorToInt(X);
		const int32 BaseY = FMath::FloorToInt(Y);
		float F1 = TNumericLimits<float>::Max();
		float F2 = TNumericLimits<float>::Max();
		for (int32 CY = BaseY - 2; CY <= BaseY + 2; ++CY)
		{
			for (int32 CX = BaseX - 2; CX <= BaseX + 2; ++CX)
			{
				const float FeatureX = static_cast<float>(CX) + 0.5f + (CracksHash01(CX, CY, Seed, 0x3211U) - 0.5f) * Jitter;
				const float FeatureY = static_cast<float>(CY) + 0.5f + (CracksHash01(CX, CY, Seed, 0x8f21U) - 0.5f) * Jitter;
				const float DX = X - FeatureX;
				const float DY = Y - FeatureY;
				const float Distance = FMath::Sqrt(DX * DX + DY * DY);
				if (Distance < F1) { F2 = F1; F1 = Distance; }
				else if (Distance < F2) F2 = Distance;
			}
		}
		return FMath::Max(F2 - F1, 0.0f);
	}

	float CracksProfile(float EdgeDistance, FName Style, float Width)
	{
		const float T = FMath::Clamp(EdgeDistance / FMath::Max(Width, UE_SMALL_NUMBER), 0.0f, 1.0f);
		if (Style == TEXT("Hard")) return T < 1.0f ? 1.0f : 0.0f;
		if (Style == TEXT("Classic")) return T < 0.22f ? 1.0f : 0.0f;
		const float Smooth = T * T * (3.0f - 2.0f * T);
		return 1.0f - Smooth;
	}

	FGaeaGridDomain ResolveDomain(const FGaeaTerrainNode& Node, const FGaeaTerrainEvaluationContext& Context)
	{
		const int32 RequestedX = Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257;
		const int32 RequestedY = Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RequestedX;
		const int32 LegacyResolution = static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 0));
		const int32 Width = FMath::Clamp(LegacyResolution > 1 ? LegacyResolution : RequestedX, 2, 4097);
		const int32 Height = FMath::Clamp(LegacyResolution > 1 ? LegacyResolution : RequestedY, 2, 4097);

		double WidthCm = 100000.0;
		double DepthCm = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WidthCm = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			DepthCm = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		else
		{
			const double LegacyWorld = FMath::Max(Node.GetNumber(TEXT("WorldSize"), 100000.0), 1.0);
			WidthCm = LegacyWorld;
			DepthCm = LegacyWorld;
		}
		return FGaeaGridDomain::Make(FIntPoint(Width, Height), FVector2d(-WidthCm * 0.5, -DepthCm * 0.5), FVector2d(WidthCm * 0.5, DepthCm * 0.5));
	}

	float ResolveHeightScale(const FGaeaTerrainNode& Node, const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale()) return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		return FMath::Max(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), Context.HeightScale)), 1.0f);
	}

	bool EvaluateCracksNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaGridDomain Domain = ResolveDomain(Node, Context);
		if (!Domain.IsValid()) { Error = TEXT("Cracks produced an invalid grid domain."); return false; }

		const FName Style = Node.GetName(TEXT("Style"), TEXT("Normal"));
		if (Style != TEXT("Normal") && Style != TEXT("Hard") && Style != TEXT("Classic")) { Error = TEXT("Cracks Style must be Normal, Hard, or Classic."); return false; }
		const int32 Octaves = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 1)), 1, 8);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.01f, 32.0f);
		const float Depth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"), 0.5)), 0.0f, 1.0f);
		const float Jitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Jitter"), 0.65)), 0.0f, 1.5f);
		const float WarpStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpStrength"), 0.0)), 0.0f, 4.0f);
		const float WarpSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpSize"), 1.0)), 0.01f, 32.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.01f, 100.0f);
		const float ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.01f, 100.0f);

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor, 0.0f);

		const FVector2d WorldExtent = Domain.WorldSize();
		const double ReferenceWorld = FMath::Max(FMath::Min(FMath::Abs(WorldExtent.X), FMath::Abs(WorldExtent.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double BaseCellWorldSize = FMath::Max(ReferenceWorld * 0.12 * Scale, 1.0);
		const float BaseWidth = FMath::Lerp(0.012f, 0.18f, Depth);
		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				float Combined = 0.0f;
				float Weight = 1.0f;
				for (int32 Octave = 0; Octave < Octaves; ++Octave)
				{
					const float FrequencyMultiplier = FMath::Pow(2.0f, static_cast<float>(Octave));
					const double CellWorldSize = BaseCellWorldSize / FrequencyMultiplier;
					float PX = static_cast<float>(World.X / (CellWorldSize * ScaleX));
					float PY = static_cast<float>(World.Y / (CellWorldSize * ScaleY));
					if (WarpStrength > UE_SMALL_NUMBER)
					{
						const float WarpFrequency = 1.0f / FMath::Max(WarpSize, 0.01f);
						const float WX = CracksSmoothNoise(PX * WarpFrequency, PY * WarpFrequency, Seed + Octave * 101, 0x514bU) - 0.5f;
						const float WY = CracksSmoothNoise(PX * WarpFrequency, PY * WarpFrequency, Seed + Octave * 101, 0xb7d2U) - 0.5f;
						PX += WX * WarpStrength;
						PY += WY * WarpStrength;
					}
					const float EdgeDistance = CracksVoronoiEdge(PX, PY, Jitter, Seed + Octave * 7919);
					const float Width = BaseWidth * FMath::Lerp(1.0f, 0.7f, static_cast<float>(Octave) / FMath::Max(static_cast<float>(Octaves - 1), 1.0f));
					const float Crack = CracksProfile(EdgeDistance, Style, Width);
					Combined = FMath::Max(Combined, Crack * Weight);
					Weight *= 0.72f;
				}
				Height.AtInterior(X, Y) = FMath::Clamp(Combined * Depth, 0.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("Cracks could not publish its Height field."); return false; }
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Node, Context));
		if (!Result.IsValid()) { Error = TEXT("Cracks produced an invalid terrain value."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaCracksNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Cracks;
	Descriptor.DisplayName = TEXT("Cracks");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Creates large cracked patterns on a flat base for masking and subtractive terrain workflows.");
	Descriptor.Outputs.Add(CracksTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(CracksNameParameter(TEXT("Style"), TEXT("Style"), TEXT("Normal"), { TEXT("Normal"), TEXT("Hard"), TEXT("Classic") }, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksIntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 1, 1, 8, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("Scale"), TEXT("Scale"), 1.0, 0.01, 32.0, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("Depth"), TEXT("Depth"), 0.5, 0.0, 1.0, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("Jitter"), TEXT("Jitter"), 0.65, 0.0, 1.5, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("WarpStrength"), TEXT("Warp Strength"), 0.0, 0.0, 4.0, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("WarpSize"), TEXT("Warp Size"), 1.0, 0.01, 32.0, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Cracks")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.01, 100.0, TEXT("Advanced Settings")));
	Descriptor.Parameters.Add(CracksNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.01, 100.0, TEXT("Advanced Settings")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Cracks, EvaluateCracksNode);
}
