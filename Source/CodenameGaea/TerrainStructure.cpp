#include "TerrainStructure.h"

#include "TerrainNoise.h"
#include "TerrainParallel.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float BuildBand(float Coordinate, float Spacing, float Width)
	{
		const float SafeSpacing = FMath::Max(Spacing, 1.0f);
		const float HalfWidth = FMath::Clamp(Width * 0.5f, 1.0f, SafeSpacing * 0.49f);
		const float Wrapped = FMath::Fmod(Coordinate + SafeSpacing * 0.5f, SafeSpacing);
		const float Centered = Wrapped < 0.0f ? Wrapped + SafeSpacing : Wrapped;
		const float Distance = FMath::Abs(Centered - SafeSpacing * 0.5f);
		return 1.0f - SmoothStep01(Distance / HalfWidth);
	}
}

void FTerrainStructure::Build(
	const FTerrainHeightField& HeightField,
	int32 Seed,
	const FTerrainStructuralSettings& Settings,
	FTerrainStructuralMaps& OutStructure)
{
	OutStructure = FTerrainStructuralMaps{};
	if (!HeightField.IsValid())
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float HalfWorldSize = HeightField.WorldSize * 0.5f;

	OutStructure.TectonicActivity.SetNumZeroed(NumCells);
	OutStructure.Uplift.SetNumZeroed(NumCells);
	OutStructure.LongValley.SetNumZeroed(NumCells);
	OutStructure.FaultWeakness.SetNumZeroed(NumCells);
	OutStructure.Bedding.SetNumZeroed(NumCells);

	FTerrainFractalNoiseSettings ActivitySettings{ 0.000035f, 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings BendSettings{ 0.000055f, 3, 0.5f, 2.0f };
	const FVector2D ActivityOffset = FTerrainNoise::MakeSeedOffset(Seed, 811);
	const FVector2D BendOffset = FTerrainNoise::MakeSeedOffset(Seed, 812);
	const FVector2D FaultBendOffset = FTerrainNoise::MakeSeedOffset(Seed, 813);

	const float DirectionRadians = FMath::DegreesToRadians(Settings.DirectionDegrees);
	const FVector2D Along(FMath::Cos(DirectionRadians), FMath::Sin(DirectionRadians));
	const FVector2D Across(-Along.Y, Along.X);

	const float FaultRadians = FMath::DegreesToRadians(Settings.DirectionDegrees + Settings.FaultAngleOffsetDegrees);
	const FVector2D FaultAlong(FMath::Cos(FaultRadians), FMath::Sin(FaultRadians));
	const FVector2D FaultAcross(-FaultAlong.Y, FaultAlong.X);

	TerrainParallel::ForRows(TEXT("TerrainStructure"), Resolution, [&](int32 StartY, int32 EndY)
	{
		for (int32 Y = StartY; Y < EndY; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = HeightField.Index(X, Y);
				const FVector2D Position(
					static_cast<float>(X) * CellSize - HalfWorldSize,
					static_cast<float>(Y) * CellSize - HalfWorldSize);

				const float ActivityNoise = FTerrainNoise::SampleFractal(Position, ActivityOffset, ActivitySettings) * 0.5f + 0.5f;
				const float Activity = SmoothStep01((ActivityNoise - (1.0f - Settings.TectonicCoverage)) / FMath::Max(Settings.TectonicCoverage, 0.01f));
				OutStructure.TectonicActivity[Index] = Activity;

				const float BendNoise = FTerrainNoise::SampleFractal(Position, BendOffset, BendSettings);
				const float BendAmount = BendNoise * Settings.DirectionVariation;
				const float AcrossCoordinate = FVector2D::DotProduct(Position, Across) + FVector2D::DotProduct(Position, Along) * BendAmount;

				const float UpliftBand = BuildBand(AcrossCoordinate, Settings.UpliftSpacing, Settings.UpliftWidth);
				OutStructure.Uplift[Index] = UpliftBand * Activity;

				const float ValleyBand = BuildBand(
					AcrossCoordinate + Settings.LongValleySpacing * 0.5f,
					Settings.LongValleySpacing,
					Settings.LongValleyWidth);
				OutStructure.LongValley[Index] = ValleyBand * Activity;

				const float FaultBend = FTerrainNoise::SampleFractal(Position, FaultBendOffset, BendSettings) * Settings.DirectionVariation;
				const float FaultCoordinate = FVector2D::DotProduct(Position, FaultAcross) + FVector2D::DotProduct(Position, FaultAlong) * FaultBend;
				const float FaultBand = BuildBand(FaultCoordinate, Settings.FaultSpacing, Settings.FaultWidth);
				OutStructure.FaultWeakness[Index] = FaultBand * Activity * FMath::Clamp(Settings.FaultWeakness, 0.0f, 1.0f);

				const float BeddingCoordinate = FVector2D::DotProduct(Position, Across);
				const float BeddingWave = 0.5f + 0.5f * FMath::Sin(BeddingCoordinate * UE_TWO_PI / FMath::Max(Settings.BeddingSpacing, 1.0f));
				OutStructure.Bedding[Index] = FMath::Clamp(
					0.5f + (BeddingWave - 0.5f) * Settings.BeddingContrast,
					0.0f,
					1.0f);
			}
		}
	});
}
