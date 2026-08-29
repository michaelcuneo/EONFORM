#include "EonformTerrainFractalWarp.h"

#include "EonformFastNoiseSIMDCompat.h"

namespace EonformTerrainProceduralOps
{
	namespace
	{
		constexpr float EonformMinWarpSize = 0.0001f;
		constexpr float EonformNormalizedWarpEpsilon = 0.1f;
		constexpr float EonformFinalPerturbFrequencyMultiplier = 4.0f;
		constexpr float EonformFinalPerturbWarpMultiplier = 0.25f;
		constexpr float EonformDirectionCycleDegrees = 360.0f;
		constexpr uint64 CoordinateBits = 21ull;
		constexpr uint64 CoordinateMask = (1ull << CoordinateBits) - 1ull;

		float MirrorCoord(float V, int32 MaxIndex)
		{
			if (MaxIndex <= 0) return 0.0f;
			const float Period = static_cast<float>(MaxIndex * 2);
			float R = FMath::Fmod(V, Period);
			if (R < 0.0f) R += Period;
			return R > MaxIndex ? Period - R : R;
		}

		void ResolveEdgeCoordinate(float& X, float& Y, int32 W, int32 H, EEdgeBehaviour Edge)
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
		}

		float Sample(const FEonformScalarField& Field, float X, float Y, EEdgeBehaviour Edge)
		{
			return FractalWarpSampleBilinear(
				[&Field](int32 SX, int32 SY) { return Field.AtInterior(SX, SY); },
				Field.Domain.Dimensions,
				X,
				Y,
				Edge);
		}

		FVector2D SampleVectorField(const TArray<FVector2D>& Field, int32 W, int32 H, float X, float Y, EEdgeBehaviour Edge)
		{
			ResolveEdgeCoordinate(X, Y, W, H, Edge);
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

		float SampleFastNoiseWarp(float X, float Y, float Z, float Frequency, FName NoiseType, int32 Octaves, float Roughness, float Jitter, int32 Seed)
		{
			NoiseType = CanonicalWarpNoiseType(NoiseType);
			const float NX = X * Frequency;
			const float NY = Y * Frequency;
			const float NZ = Z * Frequency;
			if (NoiseType == TEXT("Perlin FBM"))
			{
				return EonformFastNoiseSIMDCompat::PerlinFBM(NX, NY, NZ, FMath::Max(Octaves, 1), 2.0f, Roughness, Seed);
			}
			const EonformFastNoiseSIMDCompat::FCellularSample Cellular = EonformFastNoiseSIMDCompat::Cellular(
				NX, NY, NZ, Jitter, EonformFastNoiseSIMDCompat::ECellularDistance::Euclidean, Seed, true);
			if (NoiseType == TEXT("Voronoi R")) return Cellular.F1;
			if (NoiseType == TEXT("Voronoi A")) return Cellular.F2;
			if (NoiseType == TEXT("Voronoi P")) return Cellular.F1 + Cellular.F2;
			if (NoiseType == TEXT("Voronoi S")) return Cellular.F1 * Cellular.F2;
			if (NoiseType == TEXT("Voronoi M")) return Cellular.F1 / FMath::Max(Cellular.F2, UE_SMALL_NUMBER);
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
			OutX = SampleFastNoiseWarp(X, Y, Z, Frequency, Settings.NoiseType, Settings.Octaves, Settings.Roughness, Settings.Jitter, SeedX) * Sign;
			OutY = SampleFastNoiseWarp(X, Y, Z, Frequency, Settings.NoiseType, Settings.Octaves, Settings.Roughness, 0.45f, SeedY) * Sign;
			if (Settings.bNormalized)
			{
				const float LenSq = OutX * OutX + OutY * OutY + EonformNormalizedWarpEpsilon;
				const float InvLen = 0.5f / FMath::Sqrt(LenSq);
				OutX *= InvLen;
				OutY *= InvLen;
			}
		}

		uint64 CoordinateCacheKey(int32 Iteration, int32 X, int32 Y)
		{
			check(Iteration >= 0);
			check(X >= 0 && static_cast<uint64>(X) <= CoordinateMask);
			check(Y >= 0 && static_cast<uint64>(Y) <= CoordinateMask);
			return (static_cast<uint64>(Iteration) << (CoordinateBits * 2ull))
				| (static_cast<uint64>(Y) << CoordinateBits)
				| static_cast<uint64>(X);
		}
	}

	float FractalWarpSampleBilinear(
		const TFunctionRef<float(int32, int32)>& SampleInteger,
		const FIntPoint& Dimensions,
		float X,
		float Y,
		EEdgeBehaviour EdgeBehaviour)
	{
		const int32 W = Dimensions.X;
		const int32 H = Dimensions.Y;
		ResolveEdgeCoordinate(X, Y, W, H, EdgeBehaviour);
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
		const float TX = X - static_cast<float>(X0);
		const float TY = Y - static_cast<float>(Y0);
		return FMath::Lerp(
			FMath::Lerp(SampleInteger(X0, Y0), SampleInteger(X1, Y0), TX),
			FMath::Lerp(SampleInteger(X0, Y1), SampleInteger(X1, Y1), TX),
			TY);
	}

	bool FractalWarpVectorCoordinate(
		const FVector2D& LatticeCoordinate,
		const FIntPoint& ReferenceDimensions,
		const FFractalWarpSettings& Settings,
		FVector2D& OutSourceCoordinate,
		FString* OutError)
	{
		if (ReferenceDimensions.X < 2 || ReferenceDimensions.Y < 2)
		{
			if (OutError) *OutError = TEXT("FractalWarp point evaluation requires a valid reference resolution.");
			return false;
		}
		if (Settings.Mode != TEXT("Vector Field") || Settings.ZScale > 0.0f || Settings.Modulator)
		{
			if (OutError) *OutError = TEXT("FractalWarp point evaluation currently supports source-independent Vector Field mode only.");
			return false;
		}

		const int32 W = ReferenceDimensions.X;
		const int32 H = ReferenceDimensions.Y;
		const float Resolution = static_cast<float>(W);
		const float Size = FMath::Max(Settings.Size, EonformMinWarpSize);
		const float Frequency = 1.0f / (Size * FMath::Max(Resolution, 1.0f));
		const float Warp = Settings.Strength * Resolution * (Settings.bPersistStrength ? Size : 1.0f);
		const int32 RequestedIterations = FMath::Max(Settings.Iterations, 1);
		const bool bPerlin = CanonicalWarpNoiseType(Settings.NoiseType) == TEXT("Perlin FBM");
		const bool bNeedsPerturbationPass = !bPerlin && Settings.Perturbation != 0.0f;
		const int32 Iterations = RequestedIterations + (bNeedsPerturbationPass ? 1 : 0);

		TMap<uint64, FVector2D> IntegerCache;
		TFunction<FVector2D(int32, float, float)> SampleIteration;
		TFunction<FVector2D(int32, int32, int32)> ResolveInteger;

		SampleIteration = [&](int32 Iteration, float X, float Y) -> FVector2D
		{
			ResolveEdgeCoordinate(X, Y, W, H, Settings.EdgeBehaviour);
			if (Iteration <= 0) return FVector2D(X, Y);
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const int32 X1 = FMath::Min(X0 + 1, W - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
			const float TX = X - static_cast<float>(X0);
			const float TY = Y - static_cast<float>(Y0);
			return FMath::Lerp(
				FMath::Lerp(ResolveInteger(Iteration, X0, Y0), ResolveInteger(Iteration, X1, Y0), TX),
				FMath::Lerp(ResolveInteger(Iteration, X0, Y1), ResolveInteger(Iteration, X1, Y1), TX),
				TY);
		};

		ResolveInteger = [&](int32 Iteration, int32 X, int32 Y) -> FVector2D
		{
			if (Iteration <= 0) return FVector2D(static_cast<double>(X), static_cast<double>(Y));
			const uint64 Key = CoordinateCacheKey(Iteration, X, Y);
			if (const FVector2D* Cached = IntegerCache.Find(Key)) return *Cached;
			const int32 IterIndex = Iteration - 1;
			const bool bFinalPerturbPass = bNeedsPerturbationPass && IterIndex == Iterations - 1;
			FFractalWarpSettings IterSettings = Settings;
			float IterFrequency = Frequency;
			float IterWarp = Warp;
			float Sign = (IterIndex & 1) == 0 ? 1.0f : -1.0f;
			if (bFinalPerturbPass)
			{
				IterSettings.NoiseType = TEXT("Perlin FBM");
				IterSettings.bNormalized = false;
				IterFrequency = Frequency * EonformFinalPerturbFrequencyMultiplier;
				IterWarp = Settings.Perturbation * Resolution * Size * EonformFinalPerturbWarpMultiplier;
				Sign = 1.0f;
			}
			const int32 SeedX = Settings.Seed + IterIndex * 2;
			const int32 SeedY = SeedX + 1;
			float WX = 0.0f;
			float WY = 0.0f;
			SampleWarpVector(static_cast<float>(X), static_cast<float>(Y), 0.0f, IterFrequency, IterSettings, SeedX, SeedY, Sign, WX, WY);
			const FVector2D Result = SampleIteration(
				Iteration - 1,
				static_cast<float>(X) + WX * IterWarp,
				static_cast<float>(Y) + WY * IterWarp);
			IntegerCache.Add(Key, Result);
			return Result;
		};

		OutSourceCoordinate = SampleIteration(Iterations, static_cast<float>(LatticeCoordinate.X), static_cast<float>(LatticeCoordinate.Y));
		if (OutError) OutError->Reset();
		return true;
	}

	bool FractalWarpFidelity(
		const FEonformScalarField& Source,
		const FFractalWarpSettings& Settings,
		FEonformScalarField& OutField,
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
		const float Size = FMath::Max(Settings.Size, EonformMinWarpSize);
		const float Frequency = 1.0f / (Size * FMath::Max(Resolution, 1.0f));
		const float Warp = Settings.Strength * Resolution * (Settings.bPersistStrength ? Size : 1.0f);
		const int32 RequestedIterations = FMath::Max(Settings.Iterations, 1);
		const bool bPerlin = CanonicalWarpNoiseType(Settings.NoiseType) == TEXT("Perlin FBM");
		const bool bNeedsPerturbationPass = !bPerlin && Settings.Perturbation != 0.0f;
		float DirectionDegrees = Settings.ModulationDirectionDegrees;
		const float DirectionStep = EonformDirectionCycleDegrees / static_cast<float>(RequestedIterations);
		int32 SeedCursor = Settings.Seed;

		if (Settings.Mode == TEXT("Vector Field") && Settings.ZScale <= 0.0f && !Settings.Modulator)
		{
			OutField = Source;
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X)
				{
					FVector2D P;
					if (!FractalWarpVectorCoordinate(FVector2D(X, Y), FIntPoint(W, H), Settings, P, OutError)) return false;
					OutField.AtInterior(X, Y) = Sample(Source, static_cast<float>(P.X), static_cast<float>(P.Y), Settings.EdgeBehaviour);
				}
			}
		}
		else if (Settings.Mode == TEXT("Vector Field"))
		{
			const int32 Iterations = RequestedIterations + (bNeedsPerturbationPass ? 1 : 0);
			TArray<FVector2D> Coordinates;
			TArray<FVector2D> NextCoordinates;
			Coordinates.SetNumUninitialized(W * H);
			NextCoordinates.SetNumUninitialized(W * H);
			for (int32 Y = 0; Y < H; ++Y)
			{
				for (int32 X = 0; X < W; ++X) Coordinates[Y * W + X] = FVector2D(X, Y);
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
					IterFrequency = Frequency * EonformFinalPerturbFrequencyMultiplier;
					IterWarp = Settings.Perturbation * Resolution * Size * EonformFinalPerturbWarpMultiplier;
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
						const float Z = Settings.ZScale > 0.0f ? Sample(Source, CurrentCoord.X, CurrentCoord.Y, Settings.EdgeBehaviour) : 0.0f;
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(X, Y, Z, IterFrequency, IterSettings, SeedX, SeedY, Sign, WX, WY);
						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f) * Settings.Modulation * Resolution;
						}
						NextCoordinates[Y * W + X] = SampleVectorField(
							Coordinates, W, H,
							X + WX * IterWarp + ModAxis.X * ModOffset,
							Y + WY * IterWarp + ModAxis.Y * ModOffset,
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
					OutField.AtInterior(X, Y) = Sample(Source, P.X, P.Y, Settings.EdgeBehaviour);
				}
			}
		}
		else if (Settings.Mode == TEXT("Bitmap"))
		{
			const int32 Iterations = RequestedIterations;
			FEonformScalarField Current = Source;
			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				const float Radians = FMath::DegreesToRadians(DirectionDegrees);
				const FVector2D ModAxis(-FMath::Cos(Radians), FMath::Sin(Radians));
				DirectionDegrees += DirectionStep;
				const float Sign = (Iter & 1) == 0 ? 1.0f : -1.0f;
				const int32 SeedX = SeedCursor++;
				const int32 SeedY = SeedCursor++;
				FEonformScalarField Next = Current;
				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						const float Z = Settings.ZScale > 0.0f ? Current.AtInterior(X, Y) * Settings.ZScale : 0.0f;
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(X, Y, Z, Frequency, Settings, SeedX, SeedY, Sign, WX, WY);
						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f) * Settings.Modulation * Resolution;
						}
						Next.AtInterior(X, Y) = Sample(Current, X + WX * Warp + ModAxis.X * ModOffset, Y + WY * Warp + ModAxis.Y * ModOffset, Settings.EdgeBehaviour);
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
				for (int32 X = 0; X < W; ++X) Coordinates[Y * W + X] = FVector2D(X, Y);
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
						const float Z = Settings.ZScale > 0.0f ? Sample(Source, P.X, P.Y, Settings.EdgeBehaviour) * Settings.ZScale : 0.0f;
						float WX = 0.0f;
						float WY = 0.0f;
						SampleWarpVector(P.X, P.Y, Z, Frequency, Settings, SeedX, SeedY, Sign, WX, WY);
						float ModOffset = 0.0f;
						if (Settings.Modulator && Settings.Modulation > 0.0f)
						{
							ModOffset = (Settings.Modulator->AtInterior(X, Y) - 0.5f) * Settings.Modulation * Resolution;
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
					OutField.AtInterior(X, Y) = Sample(Source, P.X, P.Y, Settings.EdgeBehaviour);
				}
			}
		}

		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}
