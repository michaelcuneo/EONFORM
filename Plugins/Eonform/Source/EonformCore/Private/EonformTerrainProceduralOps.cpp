#include "EonformTerrainProceduralOps.h"

#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainRawNoise.h"
#include "EonformTerrainTerrace.h"
#include "Math/RandomStream.h"

namespace EonformTerrainProceduralOps
{
	bool GenerateVoronoi(const FEonformGridDomain& Domain, const FVoronoiSettings& Settings, FEonformScalarField& OutField, FString* OutError)
	{
		return EonformTerrainRawNoise::Voronoi(Domain, Settings, OutField, OutError);
	}

	bool GeneratePerlin(const FEonformGridDomain& Domain, const FPerlinSettings& Settings, FEonformScalarField& OutField, FString* OutError)
	{
		return EonformTerrainRawNoise::Perlin(Domain, Settings, OutField, OutError);
	}

	bool ApplyTerrace(const FEonformScalarField& Source, int32 NumTerraces, float Uniformity, float Steepness, float Intensity, int32 Seed, bool bForceZero, FEonformScalarField& OutField, FString* OutError)
	{
		if (!bForceZero)
		{
			return TerraceFidelity(Source, NumTerraces, Uniformity, Steepness, Intensity, Seed, OutField, OutError);
		}

		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Terrace requires a valid source field.");
			return false;
		}
		const int32 Num = FMath::Max(NumTerraces, 3);
		TArray<float> Levels;
		Levels.SetNumZeroed(Num + 1);
		const int32 EffectiveNum = Num;
		const float Spacing = 1.0f / static_cast<float>(EffectiveNum - 1);
		if (Levels.Num() > 1) Levels[1] = 0.0f;
		for (int32 I = 2; I < EffectiveNum; ++I) Levels[I] = Levels[I - 1] + Spacing;

		FRandomStream Random(Seed);
		for (int32 Pass = 0; Pass < 10; ++Pass)
		{
			for (int32 I = 1; I < EffectiveNum - 1; ++I)
			{
				const float Span = Levels[I + 1] - Levels[I - 1];
				const float Candidate = Levels[I - 1] + Span * Random.FRand();
				Levels[I] = Levels[I] * Uniformity + Candidate * (1.0f - Uniformity);
			}
		}

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Original = FMath::Clamp(Source.AtInterior(X, Y), 0.0f, 1.0f);
				int32 K = 0;
				while (K < EffectiveNum - 2 && Levels[K + 1] <= Original) ++K;
				const float Range = FMath::Max(Levels[K + 1] - Levels[K], UE_SMALL_NUMBER);
				const float T = FMath::Clamp((Original - Levels[K]) / Range, 0.0f, 1.0f);
				float Curve = 3.0f * T * T - 2.0f * T * T * T;
				Curve = (Curve - 0.5f) * 2.0f;
				const bool bNegative = Curve < 0.0f;
				Curve = FMath::Abs(Curve);
				if (bNegative) Curve = -Curve;
				Curve = Curve * 0.5f + 0.5f;
				Curve = FMath::Pow(FMath::Clamp(Curve, 0.0f, 1.0f), 1.0f - FMath::Clamp(Steepness, 0.0f, 1.0f));
				const float Terraced = Levels[K] * (1.0f - Curve) + Levels[K + 1] * Curve;
				OutField.AtInterior(X, Y) = Terraced * Intensity + Original * (1.0f - Intensity);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool FractalWarp(const FEonformScalarField& Source, const FFractalWarpSettings& Settings, FEonformScalarField& OutField, FString* OutError)
	{
		return FractalWarpFidelity(Source, Settings, OutField, OutError);
	}

	void ApplyRadialGradientMultiply(FEonformScalarField& Field, float CenterX, float CenterY, float RadiusPixels, float Height)
	{
		const float RadiusInv = 1.0f / FMath::Max(RadiusPixels, UE_SMALL_NUMBER);
		for (int32 Y = 0; Y < Field.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field.Domain.Dimensions.X; ++X)
			{
				const float DX = static_cast<float>(X) - CenterX;
				const float DY = static_cast<float>(Y) - CenterY;
				float A = FMath::Max(1.0f - FMath::Sqrt(DX * DX + DY * DY) * RadiusInv, 0.0f);
				A = A * A * (3.0f - 2.0f * A);
				Field.AtInterior(X, Y) *= A * Height;
			}
		}
	}

	void NormalizePositive(FEonformScalarField& Field)
	{
		float MaxValue = 0.0f;
		for (const float Value : Field.Values) MaxValue = FMath::Max(MaxValue, Value);
		if (MaxValue <= UE_SMALL_NUMBER) return;
		const float InvMax = 1.0f / MaxValue;
		for (float& Value : Field.Values) Value = FMath::Max(Value, 0.0f) * InvMax;
	}
}
