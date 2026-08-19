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

	// These describe/classify the negative half of the signed DEM. They do not
	// rescale or overwrite the generated elevation field.
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

	// Authoritative topology comes from TerrainBaseShape. TerrainLandmass never
	// grows or repairs islands; it only carries that topology through downstream
	// processes and derives coast/marine classifications from it.
	TArray<uint8> TopologyLandMask;

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
			&& TopologyLandMask.Num() == NumCells
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
	// Stable integration point for the generator actor. This now delegates the
	// actual regional footprint to TerrainBaseShape before local terrain synthesis.
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainStructuralMaps* Structure,
		int32 Seed,
		const FTerrainLandmassSettings& Settings,
		FTerrainLandmassMaps& OutMaps);

	// The first call composes the signed DEM from the pre-generated base-shape
	// topology. Later calls preserve that topology while refreshing classifications
	// after terrain processes. No topology search or connected-region growth occurs.
	static void RefreshSeaLevelClassification(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainLandmassSettings& Settings,
		FTerrainLandmassMaps& InOutMaps);
};
