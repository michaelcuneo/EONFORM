#include "EonformTerrainRawNoise.h"

#include "EonformFastNoiseSIMDCompat.h"
#include "EonformTerrainFieldNames.h"

namespace EonformTerrainRawNoise
{
	namespace
	{
		constexpr float RawNoiseResolutionReference = 512.0f;
		constexpr float RawNoiseFrequencyScale = 0.01f;
		constexpr float RawNoisePerturbScale = 10.0f;
		constexpr float Half = 0.5f;
		constexpr float One = 1.0f;

		void MakeHeightDescriptor(FEonformFieldDescriptor& Descriptor)
		{
			Descriptor.Name = EonformTerrainFieldNames::Height;
			Descriptor.Unit = EEonformFieldUnit::Normalized;
			Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		}

		EonformFastNoiseSIMDCompat::ECellularDistance ToDistance(FName Function)
		{
			if (Function == TEXT("Manhattan")) return EonformFastNoiseSIMDCompat::ECellularDistance::Manhattan;
			if (Function == TEXT("Natural")) return EonformFastNoiseSIMDCompat::ECellularDistance::Natural;
			return EonformFastNoiseSIMDCompat::ECellularDistance::Euclidean;
		}

		FVector2d WorldToReferenceInterior(const FVector2d& World, const FEonformGridDomain& ReferenceDomain)
		{
			const FVector2d Size = ReferenceDomain.WorldSize();
			return FVector2d(
				(World.X - ReferenceDomain.WorldMin.X) / Size.X * static_cast<double>(ReferenceDomain.Dimensions.X - 1),
				(World.Y - ReferenceDomain.WorldMin.Y) / Size.Y * static_cast<double>(ReferenceDomain.Dimensions.Y - 1));
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
			const int32 I = EonformFastNoiseSIMDCompat::PrimeCoordinate(static_cast<int32>(IF), EonformFastNoiseSIMDCompat::XPrime);
			const int32 J = EonformFastNoiseSIMDCompat::PrimeCoordinate(static_cast<int32>(JF), EonformFastNoiseSIMDCompat::YPrime);
			const int32 K = EonformFastNoiseSIMDCompat::PrimeCoordinate(static_cast<int32>(KF), EonformFastNoiseSIMDCompat::ZPrime);
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
				return T * T * EonformFastNoiseSIMDCompat::GradientCoordinate(Seed, PrimeI, PrimeJ, PrimeK, CX, CY, CZ);
			};
			return 32.0f * (
				Contribution(X0, Y0, Z0, I, J, K)
				+ Contribution(X1, Y1, Z1,
					I1 ? EonformFastNoiseSIMDCompat::WrapAdd(I, EonformFastNoiseSIMDCompat::XPrime) : I,
					J1 ? EonformFastNoiseSIMDCompat::WrapAdd(J, EonformFastNoiseSIMDCompat::YPrime) : J,
					K1 ? EonformFastNoiseSIMDCompat::WrapAdd(K, EonformFastNoiseSIMDCompat::ZPrime) : K)
				+ Contribution(X2, Y2, Z2,
					I2 ? EonformFastNoiseSIMDCompat::WrapAdd(I, EonformFastNoiseSIMDCompat::XPrime) : I,
					J2 ? EonformFastNoiseSIMDCompat::WrapAdd(J, EonformFastNoiseSIMDCompat::YPrime) : J,
					K2 ? EonformFastNoiseSIMDCompat::WrapAdd(K, EonformFastNoiseSIMDCompat::ZPrime) : K)
				+ Contribution(X3, Y3, Z3,
					EonformFastNoiseSIMDCompat::WrapAdd(I, EonformFastNoiseSIMDCompat::XPrime),
					EonformFastNoiseSIMDCompat::WrapAdd(J, EonformFastNoiseSIMDCompat::YPrime),
					EonformFastNoiseSIMDCompat::WrapAdd(K, EonformFastNoiseSIMDCompat::ZPrime)));
		}

		void Perturb(float& X, float& Y, float& Z, FName WarpType, float Amplitude, float Frequency, int32 Octaves, int32 Seed, float MainFractalBounding)
		{
			if (WarpType == TEXT("None")) return;
			if (WarpType == TEXT("Simple"))
			{
				EonformFastNoiseSIMDCompat::GradientPerturb(X, Y, Z, Seed, Amplitude, Frequency);
				return;
			}
			EonformFastNoiseSIMDCompat::GradientFractalPerturb(
				X, Y, Z, Seed, Amplitude, Frequency, Octaves, 2.0f, 0.5f, MainFractalBounding);
		}

		float CellularRaw(float X, float Y, float Z, float LookupFrequency, const EonformTerrainProceduralOps::FVoronoiSettings& Settings)
		{
			const auto DistanceType = ToDistance(Settings.Function);
			const bool bDistanceReturn = Settings.Form != TEXT("C") && Settings.Form != TEXT("D");
			const auto Sample = EonformFastNoiseSIMDCompat::Cellular(X, Y, Z, Settings.Jitter, DistanceType, Settings.Seed, bDistanceReturn);
			if (Settings.Form == TEXT("C")) return Sample.CellValue;
			if (Settings.Form == TEXT("R")) return Sample.F1;
			if (Settings.Form == TEXT("A")) return Sample.F2;
			if (Settings.Form == TEXT("P")) return Sample.F1 + Sample.F2;
			if (Settings.Form == TEXT("S")) return Sample.F1 * Sample.F2;
			if (Settings.Form == TEXT("M")) return Sample.F1 / FMath::Max(Sample.F2, UE_SMALL_NUMBER);
			if (Settings.Form == TEXT("D"))
			{
				return Simplex(Sample.Feature.X * LookupFrequency, Sample.Feature.Y * LookupFrequency, Sample.Feature.Z * LookupFrequency, Settings.Seed);
			}
			if (Settings.Form == TEXT("N"))
			{
				const float C0 = Sample.F1 / FMath::Max(Sample.F2, UE_SMALL_NUMBER);
				const auto Sample1 = EonformFastNoiseSIMDCompat::Cellular(
					X + 0.5f, Y + 0.5f, Z + 0.5f, Settings.Jitter, DistanceType, Settings.Seed + 1, true);
				const float C1 = Sample1.F1 / FMath::Max(Sample1.F2, UE_SMALL_NUMBER);
				return FMath::Min(C0, C1);
			}
			return Sample.F1 + Sample.F2;
		}

		bool IsSignedCellularOutput(FName Form)
		{
			return Form == TEXT("C") || Form == TEXT("D");
		}
	}

	float SampleVoronoiReference(
		const FVector2d& ReferenceCoordinate,
		int32 ReferenceResolutionX,
		const EonformTerrainProceduralOps::FVoronoiSettings& Settings)
	{
		const float Resolution = static_cast<float>(FMath::Max(ReferenceResolutionX, 2));
		const float ResolutionFactor = RawNoiseResolutionReference / Resolution;
		const float Frequency = Settings.Scale * RawNoiseFrequencyScale * ResolutionFactor;
		const float LookupFrequency = Settings.Scale * ResolutionFactor;
		const float Wavelength = One / Frequency;
		const float PerturbAmplitude = Settings.WarpAmplitude * RawNoisePerturbScale;
		const float DefaultFractalBounding = EonformFastNoiseSIMDCompat::FractalBounding(3, 0.5f);
		const float ResolutionInv = One / Resolution;
		float U = static_cast<float>(ReferenceCoordinate.X) * ResolutionInv;
		float V = static_cast<float>(ReferenceCoordinate.Y) * ResolutionInv;
		U += Settings.X * Wavelength * ResolutionInv;
		V += Settings.Y * Wavelength * ResolutionInv;
		U -= Half;
		V -= Half;
		float PX = U * Resolution;
		float PY = V * Resolution;
		float PZ = 0.0f;
		PX *= Frequency * Settings.ScaleX;
		PY *= Frequency * Settings.ScaleY;
		PZ *= Frequency;
		Perturb(PX, PY, PZ, Settings.WarpType, PerturbAmplitude, Settings.WarpFrequency, Settings.WarpOctaves, Settings.Seed, DefaultFractalBounding);
		const float Raw = CellularRaw(PX, PY, PZ, LookupFrequency, Settings);
		return IsSignedCellularOutput(Settings.Form) ? Half + Raw * Half : Raw * Half;
	}

	float SamplePerlinReference(
		const FVector2d& ReferenceCoordinate,
		int32 ReferenceResolutionX,
		const EonformTerrainProceduralOps::FPerlinSettings& Settings)
	{
		const float Resolution = static_cast<float>(FMath::Max(ReferenceResolutionX, 2));
		const float Scale = One - Settings.Scale;
		const float ResolutionFactor = RawNoiseResolutionReference / Resolution;
		const float MainFrequency = Scale * RawNoiseFrequencyScale * ResolutionFactor;
		const float Wavelength = One / MainFrequency;
		const float PerturbAmplitude = Settings.WarpAmplitude * RawNoisePerturbScale;
		const float ResolutionInv = One / Resolution;
		const int32 Octaves = Settings.Octaves;
		const float Gain = Settings.Gain;
		const float MainFractalBounding = EonformFastNoiseSIMDCompat::FractalBounding(Octaves, Gain);
		float U = static_cast<float>(ReferenceCoordinate.X) * ResolutionInv;
		float V = static_cast<float>(ReferenceCoordinate.Y) * ResolutionInv;
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
		Perturb(PX, PY, PZ, Settings.WarpType, PerturbAmplitude, Settings.WarpFrequency, Settings.WarpOctaves, Settings.Seed, MainFractalBounding);
		float Raw = 0.0f;
		if (Settings.Type == TEXT("Ridged"))
		{
			Raw = EonformFastNoiseSIMDCompat::PerlinRigidMulti(PX, PY, PZ, Octaves, 2.0f, Gain, Settings.Seed);
		}
		else if (Settings.Type == TEXT("Billowy"))
		{
			Raw = EonformFastNoiseSIMDCompat::PerlinBillow(PX, PY, PZ, Octaves, 2.0f, Gain, Settings.Seed);
		}
		else
		{
			Raw = EonformFastNoiseSIMDCompat::PerlinFBM(PX, PY, PZ, Octaves, 2.0f, Gain, Settings.Seed);
		}
		return Half + Raw * Half;
	}

	bool Voronoi(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FVoronoiSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError,
		const FEonformGridDomain* ReferenceDomain)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("RawNoise Voronoi requires a valid domain.");
			return false;
		}
		const FEonformGridDomain& Reference = ReferenceDomain && ReferenceDomain->IsValid() ? *ReferenceDomain : Domain;
		FEonformFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);
		const FIntPoint Storage = Domain.GetStorageDimensions();
		const bool bLegacyCoordinates = Domain.BorderSamples == 0 && Domain == Reference;
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const FVector2d ReferenceCoord = bLegacyCoordinates
					? FVector2d(X, Y)
					: WorldToReferenceInterior(Domain.StorageSampleToWorld(X, Y), Reference);
				OutField.AtStorage(X, Y) = SampleVoronoiReference(ReferenceCoord, Reference.Dimensions.X, Settings);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool Perlin(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FPerlinSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError,
		const FEonformGridDomain* ReferenceDomain)
	{
		if (!Domain.IsValid())
		{
			if (OutError) *OutError = TEXT("RawNoise Perlin requires a valid domain.");
			return false;
		}
		const FEonformGridDomain& Reference = ReferenceDomain && ReferenceDomain->IsValid() ? *ReferenceDomain : Domain;
		FEonformFieldDescriptor Descriptor;
		MakeHeightDescriptor(Descriptor);
		OutField.Initialize(Domain, Descriptor, 0.0f);
		const FIntPoint Storage = Domain.GetStorageDimensions();
		const bool bLegacyCoordinates = Domain.BorderSamples == 0 && Domain == Reference;
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const FVector2d ReferenceCoord = bLegacyCoordinates
					? FVector2d(X, Y)
					: WorldToReferenceInterior(Domain.StorageSampleToWorld(X, Y), Reference);
				OutField.AtStorage(X, Y) = SamplePerlinReference(ReferenceCoord, Reference.Dimensions.X, Settings);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
