#include "TerrainBaseShape.h"

#include "TerrainNoise.h"
#include "TerrainParallel.h"
#include "TerrainStructure.h"

namespace
{
	constexpr float LargeValue = 1.0e30f;

	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	uint32 MixBits(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}

	float Hash01(int32 Seed, int32 Salt)
	{
		const uint32 Mixed = MixBits(static_cast<uint32>(Seed) ^ MixBits(static_cast<uint32>(Salt) + 0x9e3779b9u));
		return static_cast<float>(Mixed & 0x00ffffffu) / static_cast<float>(0x01000000u);
	}

	FVector2D HashDirection(int32 Seed, int32 Salt)
	{
		const float Angle = Hash01(Seed, Salt) * UE_TWO_PI;
		return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
	}

	float EllipticalLobe(
		const FVector2D& Position,
		const FVector2D& Centre,
		const FVector2D& Axis,
		float MajorRadius,
		float MinorRadius)
	{
		const FVector2D Delta = Position - Centre;
		const FVector2D Perpendicular(-Axis.Y, Axis.X);
		const float U = FVector2D::DotProduct(Delta, Axis) / FMath::Max(MajorRadius, 1.0f);
		const float V = FVector2D::DotProduct(Delta, Perpendicular) / FMath::Max(MinorRadius, 1.0f);
		const float R2 = U * U + V * V;
		return FMath::Exp(-R2 * 1.65f);
	}

	float FindCoverageThreshold(
		const TArray<float>& Field,
		int32 Resolution,
		float Coverage,
		bool bReserveBoundary)
	{
		float MinValue = LargeValue;
		float MaxValue = -LargeValue;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				if (bReserveBoundary && (X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1))
				{
					continue;
				}
				const float Value = Field[Y * Resolution + X];
				MinValue = FMath::Min(MinValue, Value);
				MaxValue = FMath::Max(MaxValue, Value);
			}
		}

		const int32 NumCells = Resolution * Resolution;
		const int32 TargetCells = FMath::RoundToInt(FMath::Clamp(Coverage, 0.05f, 0.90f) * static_cast<float>(NumCells));
		float Low = MinValue;
		float High = MaxValue;
		for (int32 Iteration = 0; Iteration < 28; ++Iteration)
		{
			const float Threshold = (Low + High) * 0.5f;
			int32 Count = 0;
			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				for (int32 X = 0; X < Resolution; ++X)
				{
					const bool bBoundary = X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1;
					if (!(bReserveBoundary && bBoundary) && Field[Y * Resolution + X] >= Threshold)
					{
						++Count;
					}
				}
			}

			if (Count > TargetCells)
			{
				Low = Threshold;
			}
			else
			{
				High = Threshold;
			}
		}
		return (Low + High) * 0.5f;
	}
}

bool FTerrainBaseShape::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainStructuralMaps* Structure,
	int32 Seed,
	const FTerrainBaseShapeSettings& Settings,
	FTerrainBaseShapeMaps& OutMaps)
{
	OutMaps = FTerrainBaseShapeMaps{};
	if (!HeightField.IsValid())
	{
		return false;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float WorldSize = HeightField.WorldSize;
	const float HalfWorld = WorldSize * 0.5f;
	const bool bHasStructure = Structure != nullptr && Structure->IsValidFor(HeightField);
	const bool bClosedTopology = Settings.bIsland || Settings.bArchipelago;

	TArray<float> ShapeField;
	ShapeField.SetNumUninitialized(NumCells);
	OutMaps.BaseElevationCm.SetNumUninitialized(NumCells);
	OutMaps.LandInfluence.SetNumUninitialized(NumCells);
	OutMaps.TopologyLandMask.SetNumZeroed(NumCells);

	const float CoastScale = FMath::Clamp(Settings.CoastScaleCm, WorldSize * 0.12f, WorldSize * 1.5f);
	const float Irregularity = FMath::Clamp(Settings.CoastIrregularity, 0.0f, 1.0f);
	const int32 ClusterCount = Settings.bArchipelago
		? FMath::Clamp(3 + FMath::RoundToInt(Hash01(Seed, 91) * 3.0f), 3, 6)
		: 1;
	const int32 LobesPerCluster = FMath::Clamp(5 + FMath::RoundToInt(Irregularity * 4.0f), 5, 9);

	struct FLobe
	{
		FVector2D Centre = FVector2D::ZeroVector;
		FVector2D Axis = FVector2D(1.0f, 0.0f);
		float MajorRadius = 1.0f;
		float MinorRadius = 1.0f;
		float Weight = 1.0f;
	};

	TArray<FLobe> Lobes;
	Lobes.Reserve(ClusterCount * LobesPerCluster);
	for (int32 Cluster = 0; Cluster < ClusterCount; ++Cluster)
	{
		const FVector2D ClusterDirection = HashDirection(Seed, 1000 + Cluster * 13);
		const float ClusterDistance = Settings.bArchipelago
			? WorldSize * FMath::Lerp(0.12f, 0.28f, Hash01(Seed, 1100 + Cluster * 17))
			: WorldSize * FMath::Lerp(0.00f, 0.06f, Hash01(Seed, 1200));
		const FVector2D ClusterCentre = ClusterDirection * ClusterDistance;
		FVector2D Walk = ClusterCentre;
		FVector2D WalkDirection = HashDirection(Seed, 1300 + Cluster * 19);

		for (int32 LobeIndex = 0; LobeIndex < LobesPerCluster; ++LobeIndex)
		{
			const int32 Salt = Cluster * 257 + LobeIndex * 31;
			const float Turn = (Hash01(Seed, 1400 + Salt) - 0.5f) * FMath::Lerp(0.35f, 1.35f, Irregularity);
			const float C = FMath::Cos(Turn);
			const float S = FMath::Sin(Turn);
			WalkDirection = FVector2D(
				WalkDirection.X * C - WalkDirection.Y * S,
				WalkDirection.X * S + WalkDirection.Y * C).GetSafeNormal();

			if (LobeIndex > 0)
			{
				const float Step = CoastScale * FMath::Lerp(0.055f, 0.13f, Hash01(Seed, 1500 + Salt));
				Walk += WalkDirection * Step;
			}

			FLobe Lobe;
			Lobe.Centre = Walk;
			Lobe.Axis = HashDirection(Seed, 1600 + Salt);
			Lobe.MajorRadius = CoastScale * FMath::Lerp(0.20f, 0.36f, Hash01(Seed, 1700 + Salt));
			Lobe.MinorRadius = Lobe.MajorRadius * FMath::Lerp(0.45f, 0.78f, Hash01(Seed, 1800 + Salt));
			Lobe.Weight = FMath::Lerp(0.72f, 1.18f, Hash01(Seed, 1900 + Salt));
			Lobes.Add(Lobe);
		}
	}

	FTerrainFractalNoiseSettings RegionalNoise;
	RegionalNoise.Frequency = 1.0f / FMath::Max(CoastScale * FMath::Lerp(0.42f, 0.22f, Irregularity), 100.0f);
	RegionalNoise.Octaves = 4;
	RegionalNoise.Persistence = FMath::Lerp(0.38f, 0.55f, Irregularity);
	RegionalNoise.Lacunarity = 2.0f;
	const FVector2D NoiseOffset = FTerrainNoise::MakeSeedOffset(Seed, 701);
	const FVector2D WarpXOffset = FTerrainNoise::MakeSeedOffset(Seed, 702);
	const FVector2D WarpYOffset = FTerrainNoise::MakeSeedOffset(Seed, 703);
	FTerrainFractalNoiseSettings WarpNoise = RegionalNoise;
	WarpNoise.Frequency *= 0.55f;
	WarpNoise.Octaves = 3;
	const float WarpStrength = CoastScale * FMath::Lerp(0.025f, 0.16f, Irregularity);

	TerrainParallel::ForRows(TEXT("TerrainBaseShape"), Resolution, [&](int32 StartY, int32 EndY)
	{
		for (int32 Y = StartY; Y < EndY; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				const FVector2D Position(
					static_cast<float>(X) / static_cast<float>(Resolution - 1) * WorldSize - HalfWorld,
					static_cast<float>(Y) / static_cast<float>(Resolution - 1) * WorldSize - HalfWorld);
				const float WarpX = FTerrainNoise::SampleFractal(Position, WarpXOffset, WarpNoise);
				const float WarpY = FTerrainNoise::SampleFractal(Position, WarpYOffset, WarpNoise);
				const FVector2D Warped = Position + FVector2D(WarpX, WarpY) * WarpStrength;

				float LobeSum = 0.0f;
				float LobeMax = 0.0f;
				for (const FLobe& Lobe : Lobes)
				{
					const float Value = EllipticalLobe(Warped, Lobe.Centre, Lobe.Axis, Lobe.MajorRadius, Lobe.MinorRadius) * Lobe.Weight;
					LobeSum += Value;
					LobeMax = FMath::Max(LobeMax, Value);
				}

				const float Regional = FTerrainNoise::SampleFractal(Warped, NoiseOffset, RegionalNoise);
				const float Structural = bHasStructure
					? Structure->Uplift[Index] * 0.34f - Structure->LongValley[Index] * 0.08f
					: 0.0f;
				float Field = LobeMax + LobeSum * 0.20f;
				Field += Regional * FMath::Lerp(0.10f, 0.38f, Irregularity);
				Field += Structural;

				if (bClosedTopology)
				{
					const float NX = FMath::Abs(Position.X) / FMath::Max(HalfWorld, 1.0f);
					const float NY = FMath::Abs(Position.Y) / FMath::Max(HalfWorld, 1.0f);
					const float Edge = FMath::Max(NX, NY);
					const float Guard = SmoothStep01((Edge - 0.93f) / 0.07f);
					Field -= Guard * 3.0f;
				}

				ShapeField[Index] = Field;
			}
		}
	});

	const float Threshold = FindCoverageThreshold(
		ShapeField,
		Resolution,
		Settings.LandCoverage,
		bClosedTopology);
	OutMaps.SourceSeaLevelThreshold = Threshold;

	const float VerticalAmplitudeCm = FMath::Clamp(WorldSize * 0.028f, 400.0f, 6000.0f);
	const float TransitionFieldWidth = FMath::Max(0.035f, 0.16f - Irregularity * 0.08f);
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = Y * Resolution + X;
			const bool bBoundary = X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1;
			const float SignedField = ShapeField[Index] - Threshold;
			const bool bLand = SignedField >= 0.0f && !(bClosedTopology && bBoundary);
			OutMaps.TopologyLandMask[Index] = bLand ? 1 : 0;
			OutMaps.BaseElevationCm[Index] = SignedField * VerticalAmplitudeCm;
			OutMaps.LandInfluence[Index] = SmoothStep01(
				(SignedField + TransitionFieldWidth) / (TransitionFieldWidth * 2.0f));
			if (!bLand && bClosedTopology && bBoundary)
			{
				OutMaps.BaseElevationCm[Index] = FMath::Min(OutMaps.BaseElevationCm[Index], -VerticalAmplitudeCm * 0.05f);
				OutMaps.LandInfluence[Index] = 0.0f;
			}
		}
	}

	return OutMaps.IsValidFor(HeightField);
}
