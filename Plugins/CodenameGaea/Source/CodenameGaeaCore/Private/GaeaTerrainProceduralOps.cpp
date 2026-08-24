#include "GaeaTerrainProceduralOps.h"

#include "GaeaFastNoiseSIMDCompat.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainFractalWarp.h"
#include "Math/RandomStream.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
		// These are the only two remaining RawNoise wrapper calibration values that
		// have not yet been decoded from Gaea's packed constants. Keep them isolated;
		// the noise, cellular and perturb algorithms below are FastNoiseSIMD semantics.
		constexpr float UnresolvedRawNoiseFeatureCycles = 12.0f;
		constexpr float UnresolvedRawNoisePerturbFraction = 0.06f;

		float MirrorCoord(float V, int32 MaxIndex)
		{
			if (MaxIndex <= 0) return 0.0f;
			const float Period = static_cast<float>(MaxIndex * 2);
			float R = FMath::Fmod(V, Period);
			if (R < 0.0f) R += Period;
			return R > MaxIndex ? Period - R : R;
		}

		float Bilinear(const FGaeaScalarField& Field, float X, float Y, EEdgeBehaviour Edge)
		{
			const int32 W = Field.Domain.Dimensions.X;
			const int32 H = Field.Domain.Dimensions.Y;
			if (Edge == EEdgeBehaviour::Mirror)
			{
				X = MirrorCoord(X, W - 1);
				Y = MirrorCoord(Y, H - 1);
			}
			else
			{
				X = FMath::Clamp(X, 0.0f, static_cast<float>(W - 1));
				Y = FMath::Clamp(Y, 0.0f, static_cast<float>(H - 1));
			}
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const int32 X1 = FMath::Min(X0 + 1, W - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
			const float TX = X - static_cast<float>(X0);
			const float TY = Y - static_cast<float>(Y0);
			return FMath::Lerp(
				FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX),
				FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX),
				TY);
		}

		GaeaFastNoiseSIMDCompat::ECellularDistance CellularDistanceType(FName Function)
		{
			if (Function == TEXT("Manhattan")) return GaeaFastNoiseSIMDCompat::ECellularDistance::Manhattan;
			if (Function == TEXT("Natural")) return GaeaFastNoiseSIMDCompat::ECellularDistance::Natural;
			return GaeaFastNoiseSIMDCompat::ECellularDistance::Euclidean;
		}

		float FastNoiseSimplex(float X, float Y, float Z, int32 Seed)
		{
			// Scalar transcription of FastNoiseSIMD::SimplexSingle. This is used by
			// cellular NoiseLookup, whose FastNoiseSIMD default lookup type is Simplex.
			constexpr float F3 = 1.0f / 3.0f;
			constexpr float G3 = 1.0f / 6.0f;
			constexpr float G33 = -0.5f;

			const float F = (X + Y + Z) * F3;
			const float IF = FMath::FloorToFloat(X + F);
			const float JF = FMath::FloorToFloat(Y + F);
			const float KF = FMath::FloorToFloat(Z + F);
			const int32 I = GaeaFastNoiseSIMDCompat::PrimeCoordinate(static_cast<int32>(IF), GaeaFastNoiseSIMDCompat::XPrime);
			const int32 J = GaeaFastNoiseSIMDCompat::PrimeCoordinate(static_cast<int32>(JF), GaeaFastNoiseSIMDCompat::YPrime);
			const int32 K = GaeaFastNoiseSIMDCompat::PrimeCoordinate(static_cast<int32>(KF), GaeaFastNoiseSIMDCompat::ZPrime);

			const float G = (IF + JF + KF) * G3;
			const float X0 = X - (IF - G);
			const float Y0 = Y - (JF - G);
			const float Z0 = Z - (KF - G);

			const bool XGeY = X0 >= Y0;
			const bool YGeZ = Y0 >= Z0;
			const bool XGeZ = X0 >= Z0;
			const bool I1 = XGeY && XGeZ;
			const bool J1 = !XGeY && YGeZ;
			const bool K1 = !XGeZ && !YGeZ;
			const bool I2 = XGeY || XGeZ;
			const bool J2 = !XGeY || YGeZ;
			const bool K2 = !(XGeZ && YGeZ);

			const float X1 = X0 - (I1 ? 1.0f : 0.0f) + G3;
			const float Y1 = Y0 - (J1 ? 1.0f : 0.0f) + G3;
			const float Z1 = Z0 - (K1 ? 1.0f : 0.0f) + G3;
			const float X2 = X0 - (I2 ? 1.0f : 0.0f) + F3;
			const float Y2 = Y0 - (J2 ? 1.0f : 0.0f) + F3;
			const float Z2 = Z0 - (K2 ? 1.0f : 0.0f) + F3;
			const float X3 = X0 + G33;
			const float Y3 = Y0 + G33;
			const float Z3 = Z0 + G33;

			auto Contribution = [Seed](float CX, float CY, float CZ, int32 PI, int32 PJ, int32 PK)
			{
				float T = 0.6f - CX * CX - CY * CY - CZ * CZ;
				if (T < 0.0f) return 0.0f;
				T *= T;
				return T * T * GaeaFastNoiseSIMDCompat::GradientCoordinate(Seed, PI, PJ, PK, CX, CY, CZ);
			};

			const float V0 = Contribution(X0, Y0, Z0, I, J, K);
			const float V1 = Contribution(
				X1, Y1, Z1,
				I1 ? GaeaFastNoiseSIMDCompat::WrapAdd(I, GaeaFastNoiseSIMDCompat::XPrime) : I,
				J1 ? GaeaFastNoiseSIMDCompat::WrapAdd(J, GaeaFastNoiseSIMDCompat::YPrime) : J,
				K1 ? GaeaFastNoiseSIMDCompat::WrapAdd(K, GaeaFastNoiseSIMDCompat::ZPrime) : K);
			const float V2 = Contribution(
				X2, Y2, Z2,
				I2 ? GaeaFastNoiseSIMDCompat::WrapAdd(I, GaeaFastNoiseSIMDCompat::XPrime) : I,
				J2 ? GaeaFastNoiseSIMDCompat::WrapAdd(J, GaeaFastNoiseSIMDCompat::YPrime) : J,
				K2 ? GaeaFastNoiseSIMDCompat::WrapAdd(K, GaeaFastNoiseSIMDCompat::ZPrime) : K);
			const float V3 = Contribution(
				X3, Y3, Z3,
				GaeaFastNoiseSIMDCompat::WrapAdd(I, GaeaFastNoiseSIMDCompat::XPrime),
				GaeaFastNoiseSIMDCompat::WrapAdd(J, GaeaFastNoiseSIMDCompat::YPrime),
				GaeaFastNoiseSIMDCompat::WrapAdd(K, GaeaFastNoiseSIMDCompat::ZPrime));
			return 32.0f * (V0 + V1 + V2 + V3);
		}

		float CellularRaw(float X, float Y, float Z, const FVoronoiSettings& Settings)
		{
			const GaeaFastNoiseSIMDCompat::ECellularDistance DistanceType = CellularDistanceType(Settings.Function);
			const bool bDistanceReturn = Settings.Form != TEXT("C") && Settings.Form != TEXT("D");
			const GaeaFastNoiseSIMDCompat::FCellularSample S = GaeaFastNoiseSIMDCompat::Cellular(
				X, Y, Z, Settings.Jitter, DistanceType, Settings.Seed, bDistanceReturn);

			if (Settings.Form == TEXT("C")) return S.CellValue;
			if (Settings.Form == TEXT("R")) return S.F1;
			if (Settings.Form == TEXT("A")) return S.F2;
			if (Settings.Form == TEXT("P")) return S.F1 + S.F2;
			if (Settings.Form == TEXT("S")) return S.F1 * S.F2;
			if (Settings.Form == TEXT("M")) return S.F1 / FMath::Max(S.F2, UE_SMALL_NUMBER);
			if (Settings.Form == TEXT("D"))
			{
				return FastNoiseSimplex(S.Feature.X * 0.2f, S.Feature.Y * 0.2f, S.Feature.Z * 0.2f, Settings.Seed);
			}
			if (Settings.Form == TEXT("N"))
			{
				const float C0 = S.F1 / FMath::Max(S.F2, UE_SMALL_NUMBER);
				const GaeaFastNoiseSIMDCompat::FCellularSample S1 = GaeaFastNoiseSIMDCompat::Cellular(
					X + 0.5f, Y + 0.5f, Z + 0.5f, Settings.Jitter, DistanceType, Settings.Seed + 1, true);
				const float C1 = S1.F1 / FMath::Max(S1.F2, UE_SMALL_NUMBER);
				return FMath::Min(C0, C1);
			}
			return S.F1 + S.F2;
		}

		void ApplyFastNoisePerturb(
			float& X,
			float& Y,
			float& Z,
			FName WarpType,
			float PublicAmplitude,
			float Frequency,
			int32 Octaves,
			int32 Seed,
			float MainFractalBounding)
		{
			if (WarpType == TEXT("None") || PublicAmplitude <= UE_SMALL_NUMBER) return;
			if (WarpType == TEXT("Simple"))
			{
				GaeaFastNoiseSIMDCompat::GradientPerturb(X, Y, Z, Seed, PublicAmplitude, Frequency);
				return;
			}
			GaeaFastNoiseSIMDCompat::GradientFractalPerturb(
				X,
				Y,
				Z,
				Seed,
				PublicAmplitude,
				Frequency,
				FMath::Max(Octaves, 1),
				2.0f,
				0.5f,
				MainFractalBounding);
		}

		void MakeHeightDescriptor(FGaeaFieldDescriptor& Descriptor)
		{
			Descriptor.Name = GaeaTerrainFieldNames::Height;
			Descriptor.Unit = EGaeaFieldUnit::Normalized;
			Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		}
	}

	bool GenerateVoronoi(const FGaeaGridDomain& Domain, const FVoronoiSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("Voronoi requires a valid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);
		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float Scale = FMath::Clamp(Settings.Scale, 0.0001f, 4.0f);
		const float MainFrequency = Scale * UnresolvedRawNoiseFeatureCycles / FMath::Max(Resolution, 1.0f);
		const float PerturbAmplitude = Settings.WarpAmplitude * Resolution * UnresolvedRawNoisePerturbFraction;
		const float DefaultFractalBounding = GaeaFastNoiseSIMDCompat::FractalBounding(3, 0.5f);

		for (int32 Y = 0; Y < H; ++Y)
		{
			const float RawY = (H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f) * Resolution;
			for (int32 X = 0; X < W; ++X)
			{
				const float RawX = (W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f) * Resolution;

				// FastNoiseSIMD applies perturbation to xF/yF/zF after the main noise
				// frequency and axis scale have been applied. The previous implementation
				// perturbed pixel coordinates first, which is not equivalent.
				float PX = (RawX * MainFrequency + Settings.X) * Settings.ScaleX;
				float PY = (RawY * MainFrequency + Settings.Y) * Settings.ScaleY;
				float PZ = 0.0f;
				ApplyFastNoisePerturb(
					PX, PY, PZ,
					Settings.WarpType,
					PerturbAmplitude,
					Settings.WarpFrequency,
					Settings.WarpOctaves,
					Settings.Seed,
					DefaultFractalBounding);

				const float Raw = CellularRaw(PX, PY, PZ, Settings);
				// Preserve the recovered RawNoise output mapping; importantly, Ridge now
				// range-normalizes after its Min operation instead of clamping this
				// positive Distance2Add field into a constant sheet.
				OutField.AtInterior(X, Y) = 0.5f + Raw * 0.5f;
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool GeneratePerlin(const FGaeaGridDomain& Domain, const FPerlinSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("Perlin requires a valid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);
		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float ScaleTerm = FMath::Max(1.0f - FMath::Clamp(Settings.Scale, 0.0f, 1.0f), 0.0001f);
		const float MainFrequency = ScaleTerm * UnresolvedRawNoiseFeatureCycles / FMath::Max(Resolution, 1.0f);
		const float PerturbAmplitude = Settings.WarpAmplitude * Resolution * UnresolvedRawNoisePerturbFraction;
		const int32 Octaves = FMath::Clamp(Settings.Octaves, 1, 14);
		const float Gain = FMath::Clamp(Settings.Gain, 0.0f, 1.0f);
		const float MainFractalBounding = GaeaFastNoiseSIMDCompat::FractalBounding(Octaves, Gain);

		for (int32 Y = 0; Y < H; ++Y)
		{
			const float RawY = (H > 1 ? static_cast<float>(Y) / static_cast<float>(H - 1) - 0.5f : 0.0f) * Resolution;
			for (int32 X = 0; X < W; ++X)
			{
				const float RawX = (W > 1 ? static_cast<float>(X) / static_cast<float>(W - 1) - 0.5f : 0.0f) * Resolution;
				float PX = (RawX * MainFrequency + Settings.X) * Settings.ScaleX;
				float PY = (RawY * MainFrequency + Settings.Y) * Settings.ScaleY;
				float PZ = 0.0f;
				ApplyFastNoisePerturb(
					PX, PY, PZ,
					Settings.WarpType,
					PerturbAmplitude,
					Settings.WarpFrequency,
					Settings.WarpOctaves,
					Settings.Seed,
					MainFractalBounding);

				float Raw = 0.0f;
				if (Settings.Type == TEXT("Ridged"))
				{
					Raw = GaeaFastNoiseSIMDCompat::PerlinRigidMulti(PX, PY, PZ, Octaves, 2.0f, Gain, Settings.Seed);
				}
				else if (Settings.Type == TEXT("Billowy"))
				{
					Raw = GaeaFastNoiseSIMDCompat::PerlinBillow(PX, PY, PZ, Octaves, 2.0f, Gain, Settings.Seed);
				}
				else
				{
					Raw = GaeaFastNoiseSIMDCompat::PerlinFBM(PX, PY, PZ, Octaves, 2.0f, Gain, Settings.Seed);
				}
				OutField.AtInterior(X, Y) = 0.5f + Raw * 0.5f;
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool ApplyTerrace(const FGaeaScalarField& Source, int32 NumTerraces, float Uniformity, float Steepness, float Intensity, int32 Seed, bool bForceZero, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Terrace requires a valid source field.");
			return false;
		}
		const int32 Num = FMath::Max(NumTerraces, 3);
		TArray<float> Levels;
		Levels.SetNumZeroed(bForceZero ? Num + 1 : Num);
		const int32 EffectiveNum = Num;
		const float Spacing = 1.0f / static_cast<float>(EffectiveNum - 1);
		if (bForceZero && Levels.Num() > 1) Levels[1] = 0.0f;
		for (int32 I = bForceZero ? 2 : 1; I < EffectiveNum; ++I) Levels[I] = Levels[I - 1] + Spacing;

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

	bool DirectionWarpPixels(const FGaeaScalarField& Source, const FGaeaScalarField& Custom, float StrengthPixels, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FGaeaScalarField& OutField, FString* OutError)
	{
		if (!Source.IsValid() || !Custom.IsValid() || Source.Domain != Custom.Domain)
		{
			if (OutError) *OutError = TEXT("Directional warp requires matching valid source/custom fields.");
			return false;
		}
		const float Radians = FMath::DegreesToRadians(DirectionDegrees);
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

	bool DirectionWarpNormalized(const FGaeaScalarField& Source, const FGaeaScalarField& Custom, float Strength, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FGaeaScalarField& OutField, FString* OutError)
	{
		return DirectionWarpPixels(Source, Custom, Strength * static_cast<float>(Source.Domain.Dimensions.X), DirectionDegrees, EdgeBehaviour, OutField, OutError);
	}

	bool FractalWarp(const FGaeaScalarField& Source, const FFractalWarpSettings& Settings, FGaeaScalarField& OutField, FString* OutError)
	{
		return FractalWarpFidelity(Source, Settings, OutField, OutError);
	}

	void ApplyRadialGradientMultiply(FGaeaScalarField& Field, float CenterX, float CenterY, float RadiusPixels, float Height)
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

	void NormalizePositive(FGaeaScalarField& Field)
	{
		float MaxValue = 0.0f;
		for (const float Value : Field.Values) MaxValue = FMath::Max(MaxValue, Value);
		if (MaxValue <= UE_SMALL_NUMBER) return;
		const float InvMax = 1.0f / MaxValue;
		for (float& Value : Field.Values) Value = FMath::Max(Value, 0.0f) * InvMax;
	}
}
