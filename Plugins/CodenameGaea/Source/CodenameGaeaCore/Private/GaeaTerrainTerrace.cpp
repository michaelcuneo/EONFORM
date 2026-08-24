#include "GaeaTerrainTerrace.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
		class FXorShift64Star
		{
		public:
			explicit FXorShift64Star(int32 Seed)
				: State(static_cast<uint64>(static_cast<int64>(Seed)))
			{
				// xorshift64* has an all-zero lock state. Gaea normally supplies a
				// non-zero node seed here; keep zero deterministic rather than using
				// platform entropy.
				if (State == 0)
				{
					State = 0x9E3779B97F4A7C15ull;
				}
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
				// Use the complete 64-bit result and round once to float. The exact
				// Gaea Engine float conversion is isolated here so it can be replaced
				// verbatim if its tiny XorShift64Star method differs.
				constexpr double InvTwoTo64 = 1.0 / 18446744073709551616.0;
				return static_cast<float>(static_cast<double>(NextUInt64()) * InvTwoTo64);
			}

		private:
			uint64 State;
		};
	}

	bool TerraceFidelity(
		const FGaeaScalarField& Source,
		int32 NumTerraces,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FGaeaScalarField& OutField,
		FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Terrace requires a valid source field.");
			return false;
		}
		if (NumTerraces < 2)
		{
			if (OutError) *OutError = TEXT("Terrace requires at least two terrace levels.");
			return false;
		}

		// QuadSpinner.Gaea.Nodes.Profiles.Terrace, forceZero=false path.
		// Do not clamp Uniformity/Steepness/Intensity or source samples here:
		// the decompiled Gaea 2.3.0.1 implementation does not do so.
		TArray<float> Terraces;
		Terraces.SetNumZeroed(NumTerraces);

		const float Step = 1.0f / static_cast<float>(NumTerraces - 1);
		for (int32 I = 1; I < NumTerraces; ++I)
		{
			Terraces[I] = Terraces[I - 1] + Step;
		}

		FXorShift64Star Random(Seed);
		for (int32 Pass = 0; Pass < 10; ++Pass)
		{
			for (int32 K = 1; K < NumTerraces - 1; ++K)
			{
				const float Span = Terraces[K + 1] - Terraces[K - 1];
				const float Random01 = Random.NextFloat01();
				const float Candidate = Terraces[K - 1] + Span * Random01;
				Terraces[K] = Terraces[K] * Uniformity + Candidate * (1.0f - Uniformity);
			}
		}

		OutField = Source;
		for (int32 I = 0; I < OutField.Values.Num(); ++I)
		{
			const float Original = Source.Values[I];

			// The source only enters the terrace interpolation path below the top
			// of the normalized domain. Values >= 1 are preserved unchanged.
			if (Original < 1.0f)
			{
				int32 Level = 0;
				for (int32 L = 0; L < NumTerraces - 1; ++L)
				{
					if (Terraces[Level + 1] > Original)
					{
						break;
					}
					if (Level + 1 == NumTerraces)
					{
						break;
					}
					++Level;
				}

				// The source algorithm performs these operations literally, including
				// the apparently redundant sign round-trip and without input clamping.
				const float Range = Terraces[Level + 1] - Terraces[Level];
				const float T = (Original - Terraces[Level]) / Range;
				float Curve = 3.0f * T * T - 2.0f * T * T * T;
				Curve = (Curve - 0.5f) * 2.0f;
				const bool bNegative = Curve < 0.0f;
				Curve = FMath::Abs(Curve);
				if (bNegative)
				{
					Curve = -Curve;
				}
				Curve = Curve / 2.0f + 0.5f;
				Curve = FMath::Pow(Curve, 1.0f - Steepness);

				OutField.Values[I] =
					(Terraces[Level] * (1.0f - Curve) + Terraces[Level + 1] * Curve) * Intensity
					+ Original * (1.0f - Intensity);
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
