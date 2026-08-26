#include "EonformTerrainProceduralOps.h"

#include "EonformTerrainFractalWarp.h"
#include "EonformTerrainRawNoise.h"
#include "EonformTerrainTerrace.h"
#include "Math/RandomStream.h"

namespace EonformTerrainProceduralOps
{
	namespace
	{
		constexpr float GaeaDirectionDegreesToRadians = 0.017453292f; // e002(95)

		int32 MirrorIndex(int32 Index, int32 Resolution)
		{
			if (Resolution <= 0) return 0;
			Index = FMath::Abs(Index);
			const int32 Period = Resolution * 2;
			Index %= Period;
			return Index < Resolution ? Index : (Period - 1 - Index);
		}

		float Bilinear(const FEonformScalarField& Field, float X, float Y, EEdgeBehaviour Edge)
		{
			const int32 W = Field.Domain.Dimensions.X;
			const int32 H = Field.Domain.Dimensions.Y;

			// Match Gaea Warps.InterpolateBilinear exactly: integer conversion happens
			// before boundary handling, the +1 neighbours are formed before clamping or
			// mirroring, and the fractional weights are retained from those raw indices.
			int32 X0 = static_cast<int32>(X);
			int32 Y0 = static_cast<int32>(Y);
			int32 X1 = X0 + 1;
			int32 Y1 = Y0 + 1;
			const float TX = X - static_cast<float>(X0);
			const float TY = Y - static_cast<float>(Y0);

			if (Edge == EEdgeBehaviour::Edge)
			{
				X0 = FMath::Clamp(X0, 0, W - 1);
				Y0 = FMath::Clamp(Y0, 0, H - 1);
				X1 = FMath::Clamp(X1, 0, W - 1);
				Y1 = FMath::Clamp(Y1, 0, H - 1);
			}
			else
			{
				X0 = MirrorIndex(X0, W);
				X1 = MirrorIndex(X1, W);
				Y0 = MirrorIndex(Y0, H);
				Y1 = MirrorIndex(Y1, H);
			}

			const float Top = Field.AtInterior(X0, Y0) * (1.0f - TX) + Field.AtInterior(X1, Y0) * TX;
			const float Bottom = Field.AtInterior(X0, Y1) * (1.0f - TX) + Field.AtInterior(X1, Y1) * TX;
			return Top * (1.0f - TY) + Bottom * TY;
		}
	}

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
		// The recovered Gaea Profiles.Terrace implementation used by Ridge is the
		// authoritative forceZero=false path. Keep all ordinary Terrace callers on
		// that implementation rather than the older FRandomStream approximation.
		if (!bForceZero)
		{
			return TerraceFidelity(Source, NumTerraces, Uniformity, Steepness, Intensity, Seed, OutField, OutError);
		}

		// forceZero=true has additional source behavior (.001 insertion and shifted
		// level initialization) that is not yet represented by TerraceFidelity.
		// Retain the legacy path here until that exact variant is ported.
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

	bool DirectionWarpPixels(const FEonformScalarField& Source, const FEonformScalarField& Custom, float StrengthPixels, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FEonformScalarField& OutField, FString* OutError)
	{
		if (!Source.IsValid() || !Custom.IsValid() || Source.Domain != Custom.Domain)
		{
			if (OutError) *OutError = TEXT("Directional warp requires matching valid source/custom fields.");
			return false;
		}
		const float Radians = DirectionDegrees * GaeaDirectionDegreesToRadians;
		const FVector2D Direction(-FMath::Cos(Radians) * StrengthPixels, FMath::Sin(Radians) * StrengthPixels);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const FVector2D Offset = Direction * (Custom.AtInterior(X, Y) - 0.5f);
				OutField.AtInterior(X, Y) = Bilinear(Source, X + Offset.X, Y + Offset.Y, EdgeBehaviour);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool DirectionWarpNormalized(const FEonformScalarField& Source, const FEonformScalarField& Custom, float Strength, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FEonformScalarField& OutField, FString* OutError)
	{
		return DirectionWarpPixels(Source, Custom, Strength * static_cast<float>(Source.Domain.Dimensions.X), DirectionDegrees, EdgeBehaviour, OutField, OutError);
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
