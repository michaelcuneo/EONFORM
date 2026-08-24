#include "GaeaTerrainRawNoise.h"

#include "GaeaFastNoiseSIMDCompat.h"
#include "GaeaTerrainFieldNames.h"

namespace GaeaTerrainRawNoise
{
	namespace
	{
		constexpr float RawNoiseResolutionReference = 512.0f; // e002(142)
		constexpr float RawNoiseFrequencyScale = 0.01f;       // e002(97)
		constexpr float RawNoisePerturbScale = 10.0f;        // e002(81)
		constexpr float Half = 0.5f;                         // e002(2)
		constexpr float One = 1.0f;                          // e002(5)

		void MakeHeightDescriptor(FGaeaFieldDescriptor& Descriptor)
		{
			Descriptor.Name = GaeaTerrainFieldNames::Height;
			Descriptor.Unit = EGaeaFieldUnit::Normalized;
			Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		}

		GaeaFastNoiseSIMDCompat::ECellularDistance ToDistance(FName Function)
		{
			if (Function == TEXT("Manhattan")) return GaeaFastNoiseSIMDCompat::ECellularDistance::Manhattan;
			if (Function == TEXT("Natural")) return GaeaFastNoiseSIMDCompat::ECellularDistance::Natural;
			return GaeaFastNoiseSIMDCompat::ECellularDistance::Euclidean;
		}

		float Simplex(float X, float Y, float Z, int32 Seed)
		{
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

			auto Contribution = [Seed](float CX, float CY, float CZ, int32 PrimeI, int32 PrimeJ, int32 PrimeK)
			{
				float T = 0.6f - CX * CX - CY * CY - CZ * CZ;
				if (T < 0.0f) return 0.0f;
				T *= T;
				return T * T * GaeaFastNoiseSIMDCompat::GradientCoordinate(Seed, PrimeI, PrimeJ, PrimeK, CX, CY, CZ);
			};

			return 32.0f * (
				Contribution(X0, Y0, Z0, I, J, K)
				+ Contribution(
					X1, Y1, Z1,
					I1 ? GaeaFastNoiseSIMDCompat::WrapAdd(I, GaeaFastNoiseSIMDCompat::XPrime) : I,
					J1 ? GaeaFastNoiseSIMDCompat::WrapAdd(J, GaeaFastNoiseSIMDCompat::YPrime) : J,
					K1 ? GaeaFastNoiseSIMDCompat::WrapAdd(K, GaeaFastNoiseSIMDCompat::ZPrime) : K)
				+ Contribution(
					X2, Y2, Z2,
					I2 ? GaeaFastNoiseSIMDCompat::WrapAdd(I, GaeaFastNoiseSIMDCompat::XPrime) : I,
					J2 ? GaeaFastNoiseSIMDCompat::WrapAdd(J, GaeaFastNoiseSIMDCompat::YPrime) : J,
					K2 ? GaeaFastNoiseSIMDCompat::WrapAdd(K, GaeaFastNoiseSIMDCompat::ZPrime) : K)
				+ Contribution(
					X3, Y3, Z3,
					GaeaFastNoiseSIMDCompat::WrapAdd(I, GaeaFastNoiseSIMDCompat::XPrime),
					GaeaFastNoiseSIMDCompat::WrapAdd(J, GaeaFastNoiseSIMDCompat::YPrime),
					GaeaFastNoiseSIMDCompat::WrapAdd(K, GaeaFastNoiseSIMDCompat::ZPrime)));
		}

		void Perturb(
			float& X,
			float& Y,
			float& Z,
			FName WarpType,
			float Amplitude,
			float Frequency,
			int32 Octaves,
			int32 Seed,
			float MainFractalBounding)
		{
			if (WarpType == TEXT("None")) return;
			if (WarpType == TEXT("Simple"))
			{
				GaeaFastNoiseSIMDCompat::GradientPerturb(X, Y, Z, Seed, Amplitude, Frequency);
				return;
			}

			GaeaFastNoiseSIMDCompat::GradientFractalPerturb(
				X, Y, Z,
				Seed,
				Amplitude,
				Frequency,
				Octaves,
				2.0f,
				0.5f,
				MainFractalBounding);
		}

		float CellularRaw(
			float X,
			float Y,
			float Z,
			float LookupFrequency,
			const GaeaTerrainProceduralOps::FVoronoiSettings& Settings)
		{
			const auto DistanceType = ToDistance(Settings.Function);
			const bool bDistanceReturn = Settings.Form != TEXT("C") && Settings.Form != TEXT("D");
			const auto Sample = GaeaFastNoiseSIMDCompat::Cellular(
				X, Y, Z,
				Settings.Jitter,
				DistanceType,
				Settings.Seed,
				bDistanceReturn);

			if (Settings.Form == TEXT("C")) return Sample.CellValue;
			if (Settings.Form == TEXT("R")) return Sample.F1;
			if (Settings.Form == TEXT("A")) return Sample.F2;
			if (Settings.Form == TEXT("P")) return Sample.F1 + Sample.F2;
			if (Settings.Form == TEXT("S")) return Sample.F1 * Sample.F2;
			if (Settings.Form == TEXT("M")) return Sample.F1 / FMath::Max(Sample.F2, UE_SMALL_NUMBER);
			if (Settings.Form == TEXT("D"))
			{
				return Simplex(
					Sample.Feature.X * LookupFrequency,
					Sample.Feature.Y * LookupFrequency,
					Sample.Feature.Z * LookupFrequency,
					Settings.Seed);
			}
			if (Settings.Form == TEXT("N"))
			{
				const float C0 = Sample.F1 / FMath::Max(Sample.F2, UE_SMALL_NUMBER);
				const auto Sample1 = GaeaFastNoiseSIMDCompat::Cellular(
					X + 0.5f, Y + 0.5f, Z + 0.5f,
					Settings.Jitter,
					DistanceType,
					Settings.Seed + 1,
					true);
				const float C1 = Sample1.F1 / FMath::Max(Sample1.F2, UE_SMALL_NUMBER);
				return FMath::Min(C0, C1);
			}
			return Sample.F1 + Sample.F2;
		}
	}

	bool Voronoi(
		const FGaeaGridDomain& Domain,
		const GaeaTerrainProceduralOps::FVoronoiSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("RawNoise Voronoi requires a valid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);

		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float ResolutionFactor = RawNoiseResolutionReference / Resolution;
		const float Frequency = Settings.Scale * RawNoiseFrequencyScale * ResolutionFactor;
		const float LookupFrequency = Settings.Scale * ResolutionFactor;
		const float Wavelength = One / Frequency;
		const float PerturbAmplitude = Settings.WarpAmplitude * RawNoisePerturbScale;
		const float DefaultFractalBounding = GaeaFastNoiseSIMDCompat::FractalBounding(3, 0.5f);
		const float ResolutionInv = One / Resolution;

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float U = static_cast<float>(X) * ResolutionInv;
				float V = static_cast<float>(Y) * ResolutionInv;
				// Default viewport is [0,1] and RawNoise offset is applied in wavelength space.
				U += Settings.X * Wavelength * ResolutionInv;
				V += Settings.Y * Wavelength * ResolutionInv;
				U -= Half;
				V -= Half;

				float PX = U * Resolution;
				float PY = V * Resolution;
				float PZ = 0.0f;

				// FillSampledNoiseSetVector applies frequency and axis scales before perturb.
				PX *= Frequency * Settings.ScaleX;
				PY *= Frequency * Settings.ScaleY;
				PZ *= Frequency;
				Perturb(
					PX, PY, PZ,
					Settings.WarpType,
					PerturbAmplitude,
					Settings.WarpFrequency,
					Settings.WarpOctaves,
					Settings.Seed,
					DefaultFractalBounding);

				const float Raw = CellularRaw(PX, PY, PZ, LookupFrequency, Settings);
				OutField.AtInterior(X, Y) = Half + Raw * Half;
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool Perlin(
		const FGaeaGridDomain& Domain,
		const GaeaTerrainProceduralOps::FPerlinSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("RawNoise Perlin requires a valid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);

		const int32 W = Domain.Dimensions.X;
		const int32 H = Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float Scale = One - Settings.Scale;
		const float ResolutionFactor = RawNoiseResolutionReference / Resolution;
		const float MainFrequency = Scale * RawNoiseFrequencyScale * ResolutionFactor;
		const float Wavelength = One / MainFrequency;
		const float PerturbAmplitude = Settings.WarpAmplitude * RawNoisePerturbScale;
		const float ResolutionInv = One / Resolution;
		const int32 Octaves = Settings.Octaves;
		const float Gain = Settings.Gain;
		const float MainFractalBounding = GaeaFastNoiseSIMDCompat::FractalBounding(Octaves, Gain);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				float U = static_cast<float>(X) * ResolutionInv;
				float V = static_cast<float>(Y) * ResolutionInv;
				U += Settings.X * Wavelength * ResolutionInv;
				V += Settings.Y * Wavelength * ResolutionInv;
				U -= Half;
				V -= Half;

				float PX = U * Resolution;
				float PY = V * Resolution;
				float PZ = 0.0f;
				PX *= MainFrequency * Settings.ScaleX;
				PY *= MainFrequency * Settings.ScaleY;
				PZ *= MainFrequency;

				Perturb(
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

				OutField.AtInterior(X, Y) = Half + Raw * Half;
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
