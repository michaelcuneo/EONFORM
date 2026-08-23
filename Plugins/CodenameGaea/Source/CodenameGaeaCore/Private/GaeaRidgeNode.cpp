#include "GaeaRidgeNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaTerrainNodeTypes
{
	const FName Ridge(TEXT("Ridge"));
}

namespace
{
	FGaeaTerrainPortDescriptor RidgeTerrainOut()
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = TEXT("Out");
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainParameterDescriptor RidgeNumber(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}

	FGaeaTerrainParameterDescriptor RidgeInteger(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		return P;
	}

	FGaeaGridDomain BuildRidgeDomain(const FGaeaTerrainEvaluationContext& Context)
	{
		const int32 Width = FMath::Clamp(Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257, 2, 4097);
		const int32 Height = FMath::Clamp(Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : Width, 2, 4097);

		double WorldWidthCm = 100000.0;
		double WorldDepthCm = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WorldWidthCm = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			WorldDepthCm = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}

		return FGaeaGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-WorldWidthCm * 0.5, -WorldDepthCm * 0.5),
			FVector2d(WorldWidthCm * 0.5, WorldDepthCm * 0.5));
	}

	float ResolveRidgeHeightScale(const FGaeaTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}
		return FMath::Max(Context.HeightScale, 1.0f);
	}

	uint32 RidgeHash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	float RidgeHash01(int32 X, int32 Y, int32 Seed, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		H ^= Salt;
		return static_cast<float>(RidgeHash(H) & 0x00ffffffU) / 16777215.0f;
	}

	float SmoothNoise(float X, float Y, int32 Seed, uint32 Salt = 0)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float FX = X - static_cast<float>(X0);
		const float FY = Y - static_cast<float>(Y0);
		const float SX = FX * FX * (3.0f - 2.0f * FX);
		const float SY = FY * FY * (3.0f - 2.0f * FY);
		const float A = FMath::Lerp(RidgeHash01(X0, Y0, Seed, Salt), RidgeHash01(X0 + 1, Y0, Seed, Salt), SX);
		const float B = FMath::Lerp(RidgeHash01(X0, Y0 + 1, Seed, Salt), RidgeHash01(X0 + 1, Y0 + 1, Seed, Salt), SX);
		return FMath::Lerp(A, B, SY) * 2.0f - 1.0f;
	}

	float Fbm(float X, float Y, float Frequency, int32 Octaves, float Persistence, int32 Seed, uint32 Salt)
	{
		float Sum = 0.0f;
		float Weight = 0.0f;
		float Amplitude = 1.0f;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += SmoothNoise(X * Frequency, Y * Frequency, Seed + Octave * 193, Salt + static_cast<uint32>(Octave) * 7919u) * Amplitude;
			Weight += Amplitude;
			Frequency *= 2.03f;
			Amplitude *= Persistence;
		}
		return Weight > UE_SMALL_NUMBER ? Sum / Weight : 0.0f;
	}

	struct FRidgeVoronoi
	{
		float F1 = TNumericLimits<float>::Max();
		float F2 = TNumericLimits<float>::Max();
	};

	FRidgeVoronoi SampleVoronoi(float X, float Y, int32 Seed)
	{
		const int32 BaseX = FMath::FloorToInt(X);
		const int32 BaseY = FMath::FloorToInt(Y);
		FRidgeVoronoi Result;
		for (int32 CellY = BaseY - 2; CellY <= BaseY + 2; ++CellY)
		{
			for (int32 CellX = BaseX - 2; CellX <= BaseX + 2; ++CellX)
			{
				const float FeatureX = static_cast<float>(CellX) + 0.5f + (RidgeHash01(CellX, CellY, Seed, 0x17u) - 0.5f) * 0.5f;
				const float FeatureY = static_cast<float>(CellY) + 0.5f + (RidgeHash01(CellY, CellX, Seed, 0x91u) - 0.5f) * 0.5f;
				const float DX = X - FeatureX;
				const float DY = Y - FeatureY;
				const float Distance = FMath::Sqrt(DX * DX + DY * DY);
				if (Distance < Result.F1)
				{
					Result.F2 = Result.F1;
					Result.F1 = Distance;
				}
				else if (Distance < Result.F2)
				{
					Result.F2 = Distance;
				}
			}
		}
		return Result;
	}

	float TerraceGuide(float Value, int32 X, int32 Y, int32 Seed)
	{
		constexpr int32 TerraceCount = 29;
		constexpr float Uniformity = 0.6f;
		constexpr float Steepness = 0.2f;
		const float Noise = RidgeHash01(X, Y, Seed, 0x413u) - 0.5f;
		const float Phase = Noise * (1.0f - Uniformity) / static_cast<float>(TerraceCount);
		const float Scaled = FMath::Clamp(Value + Phase, 0.0f, 1.0f) * static_cast<float>(TerraceCount);
		const float Base = FMath::FloorToFloat(Scaled);
		const float Fraction = FMath::Frac(Scaled);
		const float Sharpness = FMath::Lerp(1.0f, 10.0f, Steepness);
		return FMath::Clamp((Base + FMath::Pow(Fraction, Sharpness)) / static_cast<float>(TerraceCount), 0.0f, 1.0f);
	}

	float Bilinear(const FGaeaScalarField& Field, float X, float Y)
	{
		X = FMath::Clamp(X, 0.0f, static_cast<float>(Field.Domain.Dimensions.X - 1));
		Y = FMath::Clamp(Y, 0.0f, static_cast<float>(Field.Domain.Dimensions.Y - 1));
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, Field.Domain.Dimensions.X - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Field.Domain.Dimensions.Y - 1);
		const float TX = X - static_cast<float>(X0);
		const float TY = Y - static_cast<float>(Y0);
		return FMath::Lerp(
			FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX),
			FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX),
			TY);
	}

	bool EvaluateRidgeNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaGridDomain Domain = BuildRidgeDomain(Context);
		if (!Domain.IsValid())
		{
			Error = TEXT("Ridge produced an invalid evaluation domain.");
			return false;
		}

		FGaeaRidgeSettings Settings;
		Settings.Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.75)), 0.0001f, 1.0f);
		Settings.Height = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 0.6)), 0.0f, 3.0f);
		Settings.Definition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Definition"), 0.4)), 0.0f, 1.0f);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.0001f, 100.0f);
		Settings.ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.0001f, 100.0f);

		FGaeaScalarField Height;
		if (!FGaeaRidgeGenerator::Generate(Domain, Settings, Height, &Error))
		{
			return false;
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("Ridge could not publish Height.");
			return false;
		}

		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveRidgeHeightScale(Context));
		if (!Value.IsValid())
		{
			Error = TEXT("Ridge produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}
}

bool FGaeaRidgeGenerator::Generate(
	const FGaeaGridDomain& Domain,
	const FGaeaRidgeSettings& Settings,
	FGaeaScalarField& OutHeight,
	FString* OutError)
{
	auto Fail = [OutError](const FString& Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (!Domain.IsValid()) return Fail(TEXT("Ridge requires a valid evaluation domain."));

	const int32 Width = Domain.Dimensions.X;
	const int32 Height = Domain.Dimensions.Y;
	if (Width < 2 || Height < 2) return Fail(TEXT("Ridge requires at least a 2x2 domain."));

	const float Scale = FMath::Clamp(Settings.Scale, 0.0001f, 1.0f);
	const float Definition = FMath::Clamp(Settings.Definition, 0.0f, 1.0f);
	const float ScaleX = FMath::Max(Settings.ScaleX, 0.0001f);
	const float ScaleY = FMath::Max(Settings.ScaleY, 0.0001f);

	FGaeaFieldDescriptor Descriptor;
	Descriptor.Name = GaeaTerrainFieldNames::Height;
	Descriptor.Unit = EGaeaFieldUnit::Normalized;
	Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

	FGaeaScalarField Structure;
	Structure.Initialize(Domain, Descriptor, 0.0f);
	FGaeaScalarField Guide;
	Guide.Initialize(Domain, Descriptor, 0.0f);

	const float CellFrequency = FMath::Lerp(20.0f, 3.2f, Scale);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float V = Height > 1 ? static_cast<float>(Y) / static_cast<float>(Height - 1) - 0.5f : 0.0f;
		for (int32 X = 0; X < Width; ++X)
		{
			const float U = Width > 1 ? static_cast<float>(X) / static_cast<float>(Width - 1) - 0.5f : 0.0f;
			const FRidgeVoronoi Cell = SampleVoronoi(U * CellFrequency * ScaleX, V * CellFrequency * ScaleY, Settings.Seed);
			const float Edge = FMath::Max(Cell.F2 - Cell.F1, 0.0f);
			Structure.AtInterior(X, Y) = FMath::Pow(FMath::Clamp(1.0f - Edge * 2.2f, 0.0f, 1.0f), 1.35f);

			const float ControlNoise = Fbm(U, V, 3.4f, 13, 0.5f, Settings.Seed + 1, 0x251u);
			const float Control01 = FMath::Clamp(ControlNoise * 0.5f + 0.5f, 0.0f, 1.0f);
			Guide.AtInterior(X, Y) = TerraceGuide(Control01, X, Y, Settings.Seed + 3);
		}
	}

	FGaeaScalarField WarpedGuide = Guide;
	const float GuideWarpSamples = FMath::Lerp(3.0f, 22.0f, 1.0f - Definition);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float V = Height > 1 ? static_cast<float>(Y) / static_cast<float>(Height - 1) : 0.0f;
		for (int32 X = 0; X < Width; ++X)
		{
			const float U = Width > 1 ? static_cast<float>(X) / static_cast<float>(Width - 1) : 0.0f;
			const float WX = Fbm(U, V, 5.5f, 5, 0.52f, Settings.Seed + 2, 0x451u);
			const float WY = Fbm(U, V, 5.5f, 5, 0.52f, Settings.Seed + 2, 0x8a3u);
			const float Sampled = Bilinear(Guide, X + WX * GuideWarpSamples, Y + WY * GuideWarpSamples);
			WarpedGuide.AtInterior(X, Y) = FMath::Max(Guide.AtInterior(X, Y), Sampled);
		}
	}

	FGaeaScalarField Directed = Structure;
	const float DirectionRadians = FMath::DegreesToRadians(45.0f);
	const FVector2D Axis(FMath::Cos(DirectionRadians), FMath::Sin(DirectionRadians));
	const float DirectionStrength = Definition * 1.25f * static_cast<float>(FMath::Min(Width, Height));
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float GuideSigned = WarpedGuide.AtInterior(X, Y) * 2.0f - 1.0f;
			const float Distance = GuideSigned * DirectionStrength;
			Directed.AtInterior(X, Y) = Bilinear(Structure, X - Axis.X * Distance, Y - Axis.Y * Distance);
		}
	}

	FGaeaScalarField FractalWarped = Directed;
	const float SecondaryWarpSamples = FMath::Lerp(2.0f, 16.0f, Scale);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float V = Height > 1 ? static_cast<float>(Y) / static_cast<float>(Height - 1) : 0.0f;
		for (int32 X = 0; X < Width; ++X)
		{
			const float U = Width > 1 ? static_cast<float>(X) / static_cast<float>(Width - 1) : 0.0f;
			const float WX = Fbm(U, V, FMath::Lerp(11.0f, 4.0f, Scale), 5, 0.56f, Settings.Seed + 5, 0x17bdu);
			const float WY = Fbm(U, V, FMath::Lerp(11.0f, 4.0f, Scale), 5, 0.56f, Settings.Seed + 5, 0x2a51u);
			FractalWarped.AtInterior(X, Y) = Bilinear(Directed, X + WX * SecondaryWarpSamples, Y + WY * SecondaryWarpSamples);
		}
	}

	OutHeight.Initialize(Domain, Descriptor, 0.0f);
	float MaxValue = 0.0f;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float Combined = FMath::Min(Directed.AtInterior(X, Y), FractalWarped.AtInterior(X, Y));
			const float Shaped = FMath::Pow(FMath::Clamp(Combined, 0.0f, 1.0f), FMath::Lerp(1.55f, 0.82f, Definition));
			OutHeight.AtInterior(X, Y) = Shaped;
			MaxValue = FMath::Max(MaxValue, Shaped);
		}
	}

	if (MaxValue > UE_SMALL_NUMBER)
	{
		const float InvMax = 1.0f / MaxValue;
		for (float& Value : OutHeight.Values)
		{
			Value = FMath::Clamp(Value * InvMax * Settings.Height, 0.0f, FMath::Max(Settings.Height, 0.0f));
		}
	}

	if (OutError) OutError->Reset();
	return OutHeight.IsValid();
}

void RegisterGaeaRidgeNode()
{
	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Ridge;
	D.DisplayName = TEXT("Ridge");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Generates long branching terrain ridges from a cellular structural field, terraced guide deformation, and multi-scale warping.");
	D.Outputs.Add(RidgeTerrainOut());
	D.Parameters = {
		RidgeNumber(TEXT("Scale"), TEXT("Scale"), 0.75, 0.0001, 1.0, TEXT("Ridge")),
		RidgeNumber(TEXT("Height"), TEXT("Height"), 0.6, 0.0, 3.0, TEXT("Ridge")),
		RidgeNumber(TEXT("Definition"), TEXT("Definition"), 0.4, 0.0, 1.0, TEXT("Ridge")),
		RidgeInteger(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Ridge")),
		RidgeNumber(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.0001, 100.0, TEXT("Transform")),
		RidgeNumber(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.0001, 100.0, TEXT("Transform"))
	};

	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateRidgeNode);
}
