#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainStructuralMaps;

struct FTerrainLandmassSettings
{
	bool bIsland = true;
	bool bArchipelago = false;
	float CoastScale = 32000.0f;
	float CoastIrregularity = 0.62f;
	float LandCoverage = 0.5f;
	float EdgeOceanMargin = 0.14f;
	float CoastalLandRise = 900.0f;
	float InlandRiseWidth = 6500.0f;

	float ShelfWidth = 4500.0f;
	float ShelfDepth = 700.0f;
	float ContinentalSlopeWidth = 6500.0f;
	float BasinDepth = 5200.0f;
	float BasinRelief = 900.0f;
	float TrenchDepth = 1800.0f;
	float SeamountScale = 7000.0f;
	float SeamountHeight = 1400.0f;
};

struct FTerrainLandmassMaps
{
	TArray<float> BaseElevationCm;
	TArray<float> LandInfluence;
	TArray<float> LandMask;
	TArray<float> OceanMask;
	TArray<float> CoastMask;
	TArray<float> SignedCoastDistanceCm;
	TArray<float> BathymetryDepthCm;
	TArray<float> ShelfMask;
	TArray<float> ContinentalSlopeMask;
	TArray<float> OceanBasinMask;
	TArray<float> TrenchMask;
	TArray<float> SeamountMask;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& BaseElevationCm.Num() == NumCells
			&& LandInfluence.Num() == NumCells
			&& LandMask.Num() == NumCells
			&& OceanMask.Num() == NumCells
			&& CoastMask.Num() == NumCells
			&& SignedCoastDistanceCm.Num() == NumCells
			&& BathymetryDepthCm.Num() == NumCells
			&& ShelfMask.Num() == NumCells
			&& ContinentalSlopeMask.Num() == NumCells
			&& OceanBasinMask.Num() == NumCells
			&& TrenchMask.Num() == NumCells
			&& SeamountMask.Num() == NumCells;
	}
};

class FTerrainLandmass
{
public:
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainStructuralMaps* Structure,
		int32 Seed,
		const FTerrainLandmassSettings& Settings,
		FTerrainLandmassMaps& OutMaps);

	static void RefreshSeaLevelClassification(
		const FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainLandmassSettings& Settings,
		FTerrainLandmassMaps& InOutMaps);
};
