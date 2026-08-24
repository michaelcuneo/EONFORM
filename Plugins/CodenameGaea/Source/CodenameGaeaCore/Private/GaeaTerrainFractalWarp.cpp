#include "GaeaTerrainFractalWarp.h"

#include "GaeaFastNoiseSIMDCompat.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
		constexpr float GaeaMinWarpSize = 0.0001f;                    // e002(101)
		constexpr float GaeaNormalizedWarpEpsilon = 0.1f;             // e002(93)
		constexpr float GaeaFinalPerturbFrequencyMultiplier = 4.0f;    // e002(4)
		constexpr float GaeaFinalPerturbWarpMultiplier = 0.25f;        // e002(76)
		constexpr float GaeaDirectionCycleDegrees = 360.0f;            // e002(128)

		float MirrorCoord(float V, int32 MaxIndex)
		{
			if (MaxIndex <= 0) return 0.0f;
			const float Period = static_cast<float>(MaxIndex * 2);
			float R = FMath::Fmod(V, Period);
			if (R < 0.0f) R += Period;
			return R > MaxIndex ? Period - R : R;
		}

		float Sample(const FGaeaScalarField& Field, float X, float Y, EEdgeBehaviour Edge)
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

		FVector2D SampleVectorField(const TArray<FVector2D>& Field, int32 W, int32 H, float X, float Y, EEdgeBehaviour Edge)
		{
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
				FMath::Lerp(Field[Y0 * W + X0], Field[Y0 * W + X1], TX),
				FMath::Lerp(Field[Y1 * W + X0], Field[Y1 * W + X1], TX),
				TY);
		}

		FName CanonicalWarpNoiseType(FName Type)
		{
			if (Type == TEXT("PerlinFBM")) return TEXT("Perlin FBM");
			if (Type == TEXT("VoronoiR")) return TEXT("Voronoi R");
			if (Type == TEXT("VoronoiA")) return TEXT("Voronoi A");
			if (Type == TEXT("VoronoiP")) return TEXT("Voronoi P");
			if (Type == TEXT("VoronoiS")) return TEXT("Voronoi S");
			if (Type == TEXT("VoronoiM")) return TEXT("Voronoi M");
			if (Type == TEXT("VoronoiD")) return TEXT("Voronoi D");
			return Type;
		}

		float SampleFastNoiseWarp(
			float X,
			float Y,
			float Z,
			float Frequency,
			FName NoiseType,
			int32 Octaves,
			float Roughness,
			float Jitter,
			int32 Seed)
		{
			NoiseType = CanonicalWarpNoiseType(NoiseType);
			const float NX = X * Frequency;
			const float NY = Y * Frequency;
			const float NZ = Z * Frequency;

			if (NoiseType == TEXT("Perlin FBM"))
			{
				return GaeaFastNoiseSIMDCompat::PerlinFBM(
					NX,
					NY,
					NZ,
					FMath::Max(Octaves, 1),
					2.0f,
					Roughness,
					Seed);
			}

			const GaeaFastNoiseSIMDCompat::FCellularSample Cellular =
				GaeaFastNoiseSIMDCompat::Cellular(
					NX,
					NY,
					NZ,
					Jitter,
					GaeaFastNoiseSIMDCompat::ECellularDistance::Euclidean,
					Seed,
					true);

			if (NoiseType == TEXT("Voronoi R")) return Cellular.F1;
			if (NoiseType == TEXT("Voronoi A")) return Cellular.F2;
			if (NoiseType == TEXT("Voronoi P")) return Cellular.F1 + Cellular.F2;
			if (NoiseType == TEXT("Voronoi S")) return Cellular.F1 * Cellular.F2;
			if (NoiseType == TEXT("Voronoi M")) return Cellular.F1 / FMath::Max(Cellular.F2, UE_SMALL_NUMBER);

			if (NoiseType == TEXT("Voronoi D"))
			{
				// Gaea maps this to FastNoiseSIMD NoiseLookup. The current Mountain/Ridge
				// dependency chain does not request Voronoi D, so do not substitute a
				// different noise algorithm here.
				return Cellular.F1;
			}

			return Cellular.F1;
		}

		void SampleWarpVector(
			float X,
			float Y,
			float Z,
			float Frequency,
			const FFractalWarpSettings& Settings,
			int32 SeedX,
			int32 SeedY,
			float Sign,
			float& OutX,
			float& OutY)
		{
			OutX = SampleFastNoiseWarp(
				X,
				Y,
				Z,
				Frequency,
				Settings.NoiseType,
				Settings.Octaves,
				Settings.Roughness,
				Settings.Jitter,
				SeedX) * Sign;

			// The supplied Gaea implementation calls SetCellularJitter(jitter) on nx
			// only. ny retains FastNoiseSIMD's default cellular jitter of 0.45.
			OutY = SampleFastNoiseWarp(
				X,
				Y,
				Z,
				Frequency,
				Settings.NoiseType,
				Settings.Octaves,
				Settings.Roughness,
				0.45f,
				SeedY) * Sign;

			if (Settings.bNormalized)
			{
				const float LenSq = OutX * OutX + OutY * OutY + GaeaNormalizedWarpEpsilon;
				const float InvLen = 0.5f / FMath::Sqrt(LenSq);
				OutX *= InvLen;
				OutY *= InvLen;
			}
		}
	}

	bool FractalWarpFidelity(
		const FGaeaScalarField& Source,
		const FFractalWarpSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("FractalWarp requires a valid source field.");
			return false;
		}
		if (Settings.Modulator && Settings.Modulator->Domain != Source.Domain)
		{
			if (OutError) *OutError = TEXT("FractalWarp modulator must share the source domain.");
			return false;
		}

		const int32 W = Source.Domain.Dimensions.X;
		const int32 H = Source.Domain.Dimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float Size = FMath::Max(Settings.Size, GaeaMinWarpSize);

		const float Frequency = 1.0f / (Size * FMath::Max(Resolution, 1.0f));
		const float Warp = Settings.Strength * Resolution * (Settings.bPersistStrength ? Size : 1.0f);
		const int32 RequestedIterations = FMath::Max(Settings.Iterations, 1);
		const bool bPerlin = CanonicalWarpNoiseType(Settings.NoiseType) == TEXT("Perlin FBM");
		const bool bNeedsPerturbationPass = !bPerlin && Settings.Perturbation != 0.0f;

		float DirectionDegrees = Settings.ModulationDirectionDegrees;
		const float DirectionStep = GaeaDirectionCycleDegrees / static_cast<float>(RequestedIterations);
		int32 SeedCursor = Settings.Seed;

		if (Settings.Mode == TEXT("Vector Field"))
		{
			// Gaea's Virtual mode appends one final Perlin FBM iteration when the
			// requested warp noise is cellular and perturbation is non-zero.
			const int32 Iterations = RequestedIterations + (bNeedsPerturbationPass ? 1 : 0);
			TArray<FVector2D> Coordinates;
			TArray<FVector2D> NextCoordinates;
			Coordinates.SetNumUninitialized(W * H);
			NextCoordinates.SetNumUninitialized(W * H);
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					Coordinates[Y * W + X] = FVector2D(static_cast<double>(X), static_cast<double>(Y));
				}
			}

			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				const bool bFinalPerturbPass = bNeedsPerturbationPass && Iter == Iterations - 1;
				FFractalWarpSettings IterSettings = Settings;
				float IterFrequency = Frequency;
				float IterWarp = Warp;
				float Sign = (Iter & 1) == 0 ? 1.0f : -1.0f;
				if (bFinalPerturbPass)
				{
					IterSettings.NoiseType = TEXT("Perlin FBM");
					IterSettings.bNormalized = false;
					IterFrequency = Frequency * GaeaFinalPerturbFrequencyMultiplier;
					IterWarp = Settings.Perturbation * Resolution * Size * GaeaFinalPerturbWarpMultiplier;
					Sign = 1.0f;
				}

				const float Radians = FMath::DegreesToRadians(DirectionDegrees);
				const FVector2D ModAxis(-FMath::Cos(Radians), FMath::Sin(Radians));
				DirectionDegrees += DirectionStep;
				const int32 SeedX = SeedCursor++;
				const int32 SeedY = SeedCursor++;

				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						const FVector2D CurrentCoord = Coordinates[Y * W + X];
						// In Gaea's Virtual branch zCoeff is only used as an enable gate.
						const float Z = Settings.ZScale > 0.0f
							? Sample(Source, static_cast<float>(CurrentCoord.X), static_cast<float>(CurrentCoord.Y), Settings.EdgeBehaviour)
							: 0.0f;

						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(
							static_cast<float>(X),
							static_cast<float>(Y),
							Z,
							IterFrequency,
							IterSettings,
							SeedX,
							SeedY,
							Sign,
							WX,
							WY);

						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f)
								* Settings.Modulation
								* Resolution;
						}

						NextCoordinates[Y * W + X] = SampleVectorField(
							Coordinates,
							W,
							H,
							static_cast<float>(X) + WX * IterWarp + static_cast<float>(ModAxis.X) * ModOffset,
							static_cast<float>(Y) + WY * IterWarp + static_cast<float>(ModAxis.Y) * ModOffset,
							Settings.EdgeBehaviour);
					}
				}
				Swap(Coordinates, NextCoordinates);
			}

			OutField = Source;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const FVector2D P = Coordinates[Y * W + X];
					OutField.AtInterior(X, Y) = Sample(Source, static_cast<float>(P.X), static_cast<float>(P.Y), Settings.EdgeBehaviour);
				}
			}
		}
		else if (Settings.Mode == TEXT("Bitmap"))
		{
			const int32 Iterations = RequestedIterations;
			FGaeaScalarField Current = Source;
			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				const float Radians = FMath::DegreesToRadians(DirectionDegrees);
				const FVector2D ModAxis(-FMath::Cos(Radians), FMath::Sin(Radians));
				DirectionDegrees += DirectionStep;
				const float Sign = (Iter & 1) == 0 ? 1.0f : -1.0f;
				const int32 SeedX = SeedCursor++;
				const int32 SeedY = SeedCursor++;
				FGaeaScalarField Next = Current;

				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						const float Z = Settings.ZScale > 0.0f ? Current.AtInterior(X, Y) * Settings.ZScale : 0.0f;
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(
							static_cast<float>(X),
							static_cast<float>(Y),
							Z,
							Frequency,
							Settings,
							SeedX,
							SeedY,
							Sign,
							WX,
							WY);

						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f)
								* Settings.Modulation
								* Resolution;
						}
						Next.AtInterior(X, Y) = Sample(
							Current,
							static_cast<float>(X) + WX * Warp + static_cast<float>(ModAxis.X) * ModOffset,
							static_cast<float>(Y) + WY * Warp + static_cast<float>(ModAxis.Y) * ModOffset,
							Settings.EdgeBehaviour);
					}
				}
				Current = MoveTemp(Next);
			}
			OutField = MoveTemp(Current);
		}
		else
		{
			const int32 Iterations = RequestedIterations;
			TArray<FVector2D> Coordinates;
			Coordinates.SetNumUninitialized(W * H);
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					Coordinates[Y * W + X] = FVector2D(static_cast<double>(X), static_cast<double>(Y));
				}
			}

			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				const float Radians = FMath::DegreesToRadians(DirectionDegrees);
				const FVector2D ModAxis(-FMath::Cos(Radians), FMath::Sin(Radians));
				DirectionDegrees += DirectionStep;
				const float Sign = (Iter & 1) == 0 ? 1.0f : -1.0f;
				const int32 SeedX = SeedCursor++;
				const int32 SeedY = SeedCursor++;

				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						FVector2D& P = Coordinates[Y * W + X];
						const float Z = Settings.ZScale > 0.0f
							? Sample(Source, static_cast<float>(P.X), static_cast<float>(P.Y), Settings.EdgeBehaviour) * Settings.ZScale
							: 0.0f;
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(
							static_cast<float>(P.X),
							static_cast<float>(P.Y),
							Z,
							Frequency,
							Settings,
							SeedX,
							SeedY,
							Sign,
							WX,
							WY);

						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f)
								* Settings.Modulation
								* Resolution;
						}
						P.X += WX * Warp + ModAxis.X * ModOffset;
						P.Y += WY * Warp + ModAxis.Y * ModOffset;
					}
				}
			}

			OutField = Source;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					const FVector2D P = Coordinates[Y * W + X];
					OutField.AtInterior(X, Y) = Sample(Source, static_cast<float>(P.X), static_cast<float>(P.Y), Settings.EdgeBehaviour);
				}
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
