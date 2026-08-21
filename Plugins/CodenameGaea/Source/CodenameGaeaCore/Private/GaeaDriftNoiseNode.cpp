#include "GaeaDriftNoiseNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor DriftNoiseTerrainPort(FName Name)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainParameterDescriptor DriftNoiseNumber(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max;
		return P;
	}

	FGaeaTerrainParameterDescriptor DriftNoiseInteger(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Integer; P.DefaultInteger = Default;
		P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max);
		return P;
	}

	float DriftNoiseSample(const FVector2D& P, float Scale, float Turbulence, float DirectionDeg, int32 Seed)
	{
		const float A = FMath::DegreesToRadians(DirectionDeg);
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Side(-Dir.Y, Dir.X);
		const float Along = FVector2D::DotProduct(P, Dir) / FMath::Max(Scale, 0.001f);
		const float Across = FVector2D::DotProduct(P, Side) / FMath::Max(Scale, 0.001f);
		const float Warp = FMath::PerlinNoise2D(FVector2D(Across * 0.35f + Seed * 0.013f, Along * 0.17f)) * Turbulence;
		return FMath::PerlinNoise2D(FVector2D(Along + Warp, Across * 0.45f + Seed * 0.021f));
	}

	bool EvaluateDriftNoiseNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const int32 Passes = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Passes"), 4)), 1, 16);
		const float Chaos = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Chaos"), 0.5)), 0.0f, 2.0f);
		const float ScaleLarge = static_cast<float>(Node.GetNumber(TEXT("ScaleLarge"), 1.0));
		const float ScaleSmall = static_cast<float>(Node.GetNumber(TEXT("ScaleSmall"), 0.25));
		const float TurbulenceLarge = static_cast<float>(Node.GetNumber(TEXT("TurbulenceLarge"), 0.35));
		const float TurbulenceSmall = static_cast<float>(Node.GetNumber(TEXT("TurbulenceSmall"), 0.75));
		const float WindLarge = static_cast<float>(Node.GetNumber(TEXT("WindDirectionLarge"), 25.0));
		const float WindSmall = static_cast<float>(Node.GetNumber(TEXT("WindDirectionSmall"), 110.0));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const float Overlap = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("OverlapShapes"), 0.55)), 0.0f, 1.0f);

		const double Half = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid()) { Error = TEXT("DriftNoise produced an invalid grid domain."); return false; }
		FGaeaFieldDescriptor D; D.Name = GaeaTerrainFieldNames::Height; D.Unit = EGaeaFieldUnit::Normalized; D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field; Field.Initialize(Domain, D);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const FVector2D P(static_cast<float>(W.X / WorldSize), static_cast<float>(W.Y / WorldSize));
				float Value = 0.0f;
				float Amp = 1.0f;
				float AmpSum = 0.0f;
				for (int32 Pass = 0; Pass < Passes; ++Pass)
				{
					const float T = Passes > 1 ? static_cast<float>(Pass) / static_cast<float>(Passes - 1) : 0.0f;
					const float S = FMath::Lerp(ScaleLarge, ScaleSmall, T) * FMath::Pow(0.62f, static_cast<float>(Pass));
					const float Turb = FMath::Lerp(TurbulenceLarge, TurbulenceSmall, T);
					const float Wind = FMath::Lerp(WindLarge, WindSmall, T) + Chaos * Pass * 17.0f;
					const float N = DriftNoiseSample(P * 6.0f, S, Turb, Wind, Seed + Pass * 97);
					const float Shelf = FMath::FloorToFloat((N * 0.5f + 0.5f) * (3.0f + Overlap * 9.0f)) / FMath::Max(3.0f + Overlap * 9.0f, 1.0f);
					Value += FMath::Lerp(N * 0.5f + 0.5f, Shelf, Overlap) * Amp;
					AmpSum += Amp;
					Amp *= 0.58f;
				}
				Field.AtInterior(X, Y) = FMath::Clamp(Value / FMath::Max(AmpSum, UE_SMALL_NUMBER), 0.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("DriftNoise could not publish Height."); return false; }
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid()) { Error = TEXT("DriftNoise produced invalid terrain."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaDriftNoiseNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::DriftNoise;
	Descriptor.DisplayName = TEXT("DriftNoise");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates overlapping shelf-like directional noise.");
	Descriptor.Outputs.Add(DriftNoiseTerrainPort(TEXT("Out")));
	Descriptor.Parameters.Add(DriftNoiseInteger(TEXT("Passes"), TEXT("Passes"), 4, 1, 16));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("Chaos"), TEXT("Chaos"), 0.5, 0.0, 2.0));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("ScaleLarge"), TEXT("Scale Large"), 1.0, 0.01, 8.0));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("ScaleSmall"), TEXT("Scale Small"), 0.25, 0.01, 8.0));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("TurbulenceLarge"), TEXT("Turbulence Large"), 0.35, 0.0, 4.0));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("TurbulenceSmall"), TEXT("Turbulence Small"), 0.75, 0.0, 4.0));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("WindDirectionLarge"), TEXT("Wind Direction Large"), 25.0, -360.0, 360.0));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("WindDirectionSmall"), TEXT("Wind Direction Small"), 110.0, -360.0, 360.0));
	Descriptor.Parameters.Add(DriftNoiseInteger(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	Descriptor.Parameters.Add(DriftNoiseNumber(TEXT("OverlapShapes"), TEXT("Overlap Shapes"), 0.55, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::DriftNoise, EvaluateDriftNoiseNode);
}
