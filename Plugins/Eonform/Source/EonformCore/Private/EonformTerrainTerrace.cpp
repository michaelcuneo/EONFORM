#include "EonformTerrainTerrace.h"

namespace EonformTerrainProceduralOps
{
	namespace
	{
		class FXorShift64Star
		{
		public:
			explicit FXorShift64Star(int32 Seed)
				: State(static_cast<uint64>(static_cast<int64>(Seed)))
			{
				if (State == 0) State = 0x9E3779B97F4A7C15ull;
			}

			uint64 NextUInt64()
			{
				uint64 X = State;
				X ^= X >> 12;
				X ^= X << 25;
				X ^= X >> 27;
				State = X;
				return X * 0x2545F4914F6CDD1Dull;
			}

			float NextFloat01()
			{
				constexpr double InvTwoTo64 = 1.0 / 18446744073709551616.0;
				return static_cast<float>(static_cast<double>(NextUInt64()) * InvTwoTo64);
			}

		private:
			uint64 State;
		};
	}

	bool PrepareTerraceProfile(
		int32 NumTerraces,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FPreparedTerraceProfile& OutProfile,
		FString* OutError)
	{
		OutProfile = FPreparedTerraceProfile();
		if (NumTerraces < 2)
		{
			if (OutError) *OutError = TEXT("Terrace requires at least two terrace levels.");
			return false;
		}

		OutProfile.Levels.SetNumZeroed(NumTerraces);
		OutProfile.Steepness = Steepness;
		OutProfile.Intensity = Intensity;
		const float Step = 1.0f / static_cast<float>(NumTerraces - 1);
		for (int32 I = 1; I < NumTerraces; ++I)
		{
			OutProfile.Levels[I] = OutProfile.Levels[I - 1] + Step;
		}

		FXorShift64Star Random(Seed);
		for (int32 Pass = 0; Pass < 10; ++Pass)
		{
			for (int32 K = 1; K < NumTerraces - 1; ++K)
			{
				const float Span = OutProfile.Levels[K + 1] - OutProfile.Levels[K - 1];
				const float Candidate = OutProfile.Levels[K - 1] + Span * Random.NextFloat01();
				OutProfile.Levels[K] = OutProfile.Levels[K] * Uniformity + Candidate * (1.0f - Uniformity);
			}
		}
		if (OutError) OutError->Reset();
		return true;
	}

	float ApplyPreparedTerraceValue(float Original, const FPreparedTerraceProfile& Profile)
	{
		if (!Profile.IsValid() || Original >= 0.999f) return Original;
		int32 Level = 0;
		for (int32 L = 0; L < Profile.Levels.Num() - 1; ++L)
		{
			if (Profile.Levels[Level + 1] > Original) break;
			if (Level + 1 == Profile.Levels.Num()) break;
			++Level;
		}
		Level = FMath::Clamp(Level, 0, Profile.Levels.Num() - 2);
		const float Range = Profile.Levels[Level + 1] - Profile.Levels[Level];
		const float T = (Original - Profile.Levels[Level]) / Range;
		float Curve = 3.0f * T * T - 2.0f * T * T * T;
		Curve = (Curve - 0.5f) * 2.0f;
		const bool bNegative = Curve < 0.0f;
		Curve = FMath::Abs(Curve);
		if (bNegative) Curve = -Curve;
		Curve = Curve / 2.0f + 0.5f;
		Curve = FMath::Pow(Curve, 1.0f - Profile.Steepness);
		return (Profile.Levels[Level] * (1.0f - Curve) + Profile.Levels[Level + 1] * Curve) * Profile.Intensity
			+ Original * (1.0f - Profile.Intensity);
	}

	bool TerraceFidelity(
		const FEonformScalarField& Source,
		int32 NumTerraces,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FEonformScalarField& OutField,
		FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Terrace requires a valid source field.");
			return false;
		}

		FPreparedTerraceProfile Profile;
		if (!PrepareTerraceProfile(NumTerraces, Uniformity, Steepness, Intensity, Seed, Profile, OutError)) return false;
		OutField = Source;
		for (int32 I = 0; I < OutField.Values.Num(); ++I)
		{
			OutField.Values[I] = ApplyPreparedTerraceValue(Source.Values[I], Profile);
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
