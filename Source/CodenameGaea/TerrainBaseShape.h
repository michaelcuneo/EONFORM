#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainStructuralMaps;

struct FTerrainBaseShapeSettings
{
	bool bIsland = true;
	bool bArchipelago = false;
	float CoastScaleCm = 32000.0f;
	float CoastIrregularity = 0.62f;
	float LandCoverage = 0.5f;
	int32 MaxShapeResolution = 1025;
};

struct FTerrainBaseShapeMaps
{
	// Signed regional elevation produced before local terrain synthesis. Zero is
	// the source sea-level contour; positive values are terrestrial and negative
	// values are marine.
	TArray<float> BaseElevationCm;

	// Smooth terrestrial participation mask used only to keep later relief from
	// punching through the source coastline. It is derived from the signed base
	// shape rather than used to manufacture the shape after the fact.
	TArray<float> LandInfluence;

	// Authoritative island/archipelago topology derived directly from the base
	// shape's zero contour. TerrainLandmass consumes this topology but does not
	// grow, repair, or reshape it.
	TArray<uint8> TopologyLandMask;

	float SourceSeaLevelThreshold = 0.0f;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& BaseElevationCm.Num() == NumCells
			&& LandInfluence.Num() == NumCells
			&& TopologyLandMask.Num() == NumCells;
	}
};

class FTerrainBaseShape
{
public:
	static bool Build(
		const FTerrainHeightField& HeightField,
		const FTerrainStructuralMaps* Structure,
		int32 Seed,
		const FTerrainBaseShapeSettings& Settings,
		FTerrainBaseShapeMaps& OutMaps);
};
