#include "GaeaTerrainFractalWarp.h"

#include "GaeaFastNoiseSIMDCompat.h"

namespace GaeaTerrainProceduralOps
{
	namespace
	{
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
				// Gaea's FractalWarp configures FastNoiseSIMD as PerlinFractal/FBM,
				// sets the requested octave count and gain, and leaves FastNoiseSIMD's
				// default lacunarity at 2.0.
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
				// FastNoiseSIMD's default cellular NoiseLookup is Simplex at frequency
				// 0.2. The current Mountain/Ridge path does not request Voronoi D, so
				// do not invent a substitute for it here. Returning the nearest-distance
				// field keeps the unsupported branch deterministic until the supplied
				// NoiseLookup path is ported.
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
			OutY = SampleFastNoiseWarp(
				X,
				Y,
				Z,
				Frequency,
				Settings.NoiseType,
				Settings.Octaves,
				Settings.Roughness,
				Settings.Jitter,
				SeedY) * Sign;

			if (Settings.bNormalized)
			{
				// This normalization is not exercised by Mountain/Ridge fidelity paths,
				// but preserve the recovered shape of the operation until the remaining
				// obfuscated scalar is named from the supplied assembly.
				const float LenSq = OutX * OutX + OutY * OutY + 0.1f;
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
		const float Size = FMath::Max(Settings.Size, 0.0001f);

		// Directly recovered from QuadSpinner.Gaea.Nodes.Warps.FractalWarp:
		//   num  = size * resolution
		//   num2 = 1 / num
		//   warp = strength * resolution * (persistStrength ? size : 1)
		const float Frequency = 1.0f / (Size * FMath::Max(Resolution, 1.0f));
		const float Warp = Settings.Strength * Resolution * (Settings.bPersistStrength ? Size : 1.0f);
		const int32 RequestedIterations = FMath::Max(Settings.Iterations, 1);
		const bool bPerlin = CanonicalWarpNoiseType(Settings.NoiseType) == TEXT("Perlin FBM");
		const bool bNeedsPerturbationPass = !bPerlin && Settings.Perturbation > 0.0f;

		// The supplied implementation adds one final Perlin perturbation iteration
		// for cellular warp modes (except its special fourth iterative mode). The
		// exact perturbation amplitude scalar is still obfuscated in the supplied
		// assembly, so the source-certain Mountain/Perlin path is kept exact while
		// cellular secondary perturbation remains deliberately disabled rather than
		// replaced with another guessed coefficient.
		const int32 Iterations = RequestedIterations;
		(void)bNeedsPerturbationPass;

		float DirectionDegrees = Settings.ModulationDirectionDegrees;
		const float DirectionStep = 360.0f / static_cast<float>(RequestedIterations);
		int32 SeedCursor = Settings.Seed;

		if (Settings.Mode == TEXT("Vector Field"))
		{
			// Gaea IterativeWarpMode.Virtual: compose a coordinate field, then sample
			// the original map once at the final coordinates.
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
						const FVector2D CurrentCoord = Coordinates[Y * W + X];
						const float Z = Settings.ZScale > 0.0f
							? Sample(Source, static_cast<float>(CurrentCoord.X), static_cast<float>(CurrentCoord.Y), Settings.EdgeBehaviour) * Settings.ZScale
							: 0.0f;

						// The decompiled Gaea code feeds the original pixel X/Y into FNSIMD
						// on every Virtual iteration; only Z follows the composed coordinate
						// field when zCoeff is enabled.
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

						NextCoordinates[Y * W + X] = SampleVectorField(
							Coordinates,
							W,
							H,
							static_cast<float>(X) + WX * Warp + static_cast<float>(ModAxis.X) * ModOffset,
							static_cast<float>(Y) + WY * Warp + static_cast<float>(ModAxis.Y) * ModOffset,
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
			// Gaea IterativeWarpMode.Real: each iteration samples the current bitmap
			// into the next bitmap, then makes that result the source of the next pass.
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
			// Gaea IterativeWarpMode.Integral: accumulate displacement directly in
			// coordinate vectors, then sample the source once. This path is included
			// for contract completeness; Mountain itself uses Virtual mode.
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
