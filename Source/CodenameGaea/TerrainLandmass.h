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

	float ShelfWidth = 1500.0f;
	float ShelfDepth = 220.0f;
	float ContinentalSlopeWidth = 4000.0f;
	float BasinDepth = 1800.0f;
	float BasinRelief = 350.0f;
	float TrenchDepth = 700.0f;
	float SeamountScale = 5000.0f;
	float SeamountHeight = 700.0f;
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

	bool bCompositionApplied = false;
	float SourceSeaLevelThreshold = 0.0f;

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
	// Initializes output maps only. This function is deliberately height-neutral so
	// the existing terrain generator remains unchanged until the terrain actually exists.
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainStructuralMaps* Structure,
		int32 Seed,
		const FTerrainLandmassSettings& Settings,
		FTerrainLandmassMaps& OutMaps);

	// The first call composes the signed DEM from the already-generated natural terrain:
	// positive land, sea level at 0, and negative bathymetry extending outward from
	// the terrain-derived coastline. Later calls only refresh classifications.
	static void RefreshSeaLevelClassification(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainLandmassSettings& Settings,
		FTerrainLandmassMaps& InOutMaps);
};
