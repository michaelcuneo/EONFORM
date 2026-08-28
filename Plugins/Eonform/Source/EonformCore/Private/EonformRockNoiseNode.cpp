#include "EonformRockNoiseNode.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor TerrainPort()
	{
		FEonformTerrainPortDescriptor P; P.Name = TEXT("Out"); P.DisplayName = TEXT("Out"); P.DataType = TEXT("Terrain"); return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); return P;
	}
	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default; for (const FName V : Options) P.NameOptions.Add(V); return P;
	}
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
	}
	float Hash01(int32 X, int32 Y, int32 Seed, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U; H ^= static_cast<uint32>(Y) * 0x85ebca6bU; H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U; H ^= Salt;
		return static_cast<float>(Hash(H) & 0x00ffffffU) / 16777215.0f;
	}
	FEonformGridDomain BuildDomain(const FEonformTerrainEvaluationContext& Context)
	{
		const int32 RX = FMath::Clamp(Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257, 2, 4097);
		const int32 RY = FMath::Clamp(Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RX, 2, 4097);
		double W = 100000.0, H = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions()) { W = Context.PhysicalMetrics.WorldWidthMeters * 100.0; H = Context.PhysicalMetrics.WorldDepthMeters * 100.0; }
		return FEonformGridDomain::Make(FIntPoint(RX, RY), FVector2d(-W * 0.5, -H * 0.5), FVector2d(W * 0.5, H * 0.5));
	}
	float ResolveHeightScale(const FEonformTerrainEvaluationContext& Context)
	{
		return Context.PhysicalMetrics.HasElevationScale() ? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0) : FMath::Max(Context.HeightScale, 1.0f);
	}
	float RockStamp(float DX, float DY, float Radius, FName Style, float Variety)
	{
		const float AX = FMath::Abs(DX) / FMath::Max(Radius, 0.001f), AY = FMath::Abs(DY) / FMath::Max(Radius, 0.001f);
		float D = FMath::Sqrt(AX * AX + AY * AY);
		if (Style == TEXT("B")) D = FMath::Max(AX, AY) * FMath::Lerp(0.92f, 1.18f, Variety);
		else if (Style == TEXT("C")) D = FMath::Pow(FMath::Pow(AX, 1.45f) + FMath::Pow(AY, 1.45f), 1.0f / 1.45f);
		if (D >= 1.0f) return 0.0f;
		float V = FMath::Pow(1.0f - D * D, Style == TEXT("A") ? 1.45f : (Style == TEXT("B") ? 0.78f : 1.02f));
		if (Style == TEXT("C")) V *= 0.78f + 0.22f * FMath::Cos((DX + DY) / FMath::Max(Radius, 0.001f) * PI * 2.0f);
		return FMath::Max(V, 0.0f);
	}
	bool EvaluateRockNoise(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs&, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformGridDomain Domain = BuildDomain(Context);
		if (!Domain.IsValid()) { Error = TEXT("RockNoise produced invalid domain."); return false; }
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.02f, 4.0f);
		const float Variety = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Variety"), 0.5)), 0.0f, 1.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 8);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const FName Style = Node.GetName(TEXT("Style"), TEXT("A"));
		FEonformFieldDescriptor D; D.Name = EonformTerrainFieldNames::Height; D.Unit = EEonformFieldUnit::Normalized; D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Height; Height.Initialize(Domain, D, 0.0f);
		const int32 W = Domain.Dimensions.X, H = Domain.Dimensions.Y;
		for (int32 O = 0; O < Octaves; ++O)
		{
			const float Cells = FMath::Lerp(7.0f, 28.0f, 1.0f / FMath::Max(Size, 0.02f)) * FMath::Pow(2.0f, O);
			const float Amp = FMath::Pow(0.62f, O);
			for (int32 Y = 0; Y < H; ++Y) for (int32 X = 0; X < W; ++X)
			{
				const float U = static_cast<float>(X) / FMath::Max(W - 1, 1) * Cells;
				const float V = static_cast<float>(Y) / FMath::Max(H - 1, 1) * Cells;
				const int32 BX = FMath::FloorToInt(U), BY = FMath::FloorToInt(V);
				float Stamp = 0.0f;
				for (int32 CY = BY - 1; CY <= BY + 1; ++CY) for (int32 CX = BX - 1; CX <= BX + 1; ++CX)
				{
					if (Hash01(CX, CY, Seed + O * 991, 0x27u) < FMath::Lerp(0.58f, 0.28f, Variety)) continue;
					const float PX = CX + 0.5f + (Hash01(CX, CY, Seed, 0x31u) - 0.5f) * 0.78f;
					const float PY = CY + 0.5f + (Hash01(CY, CX, Seed, 0x47u) - 0.5f) * 0.78f;
					const float Radius = FMath::Lerp(0.16f, 0.46f, Hash01(CX, CY, Seed + O * 101, 0x59u)) * FMath::Lerp(0.72f, 1.32f, Variety);
					Stamp = FMath::Max(Stamp, RockStamp(U - PX, V - PY, Radius, Style, Variety));
				}
				Height.AtInterior(X, Y) = FMath::Max(Height.AtInterior(X, Y), Stamp * Amp);
			}
		}
		FEonformTerrainDataset Dataset; if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("RockNoise could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Context)));
		return true;
	}
}

void RegisterEonformRockNoiseNode()
{
	FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::RockNoise; D.DisplayName = TEXT("RockNoise"); D.Category = TEXT("Surface");
	D.Description = TEXT("Generates a flat multi-octave rock field for Embed/Insert workflows."); D.Outputs.Add(TerrainPort());
	D.Parameters = { Num(TEXT("Size"), TEXT("Size"), 0.5, 0.02, 4.0), Num(TEXT("Variety"), TEXT("Variety"), 0.5, 0.0, 1.0), Int(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 8), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647), Choice(TEXT("Style"), TEXT("Style"), TEXT("A"), { TEXT("A"), TEXT("B"), TEXT("C") }) };
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateRockNoise);
}
