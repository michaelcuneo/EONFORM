#pragma once

#include "CoreMinimal.h"

struct FTerrainHeightField;

class FTerrainWater
{
public:
	// Finds the first WaterBodyOcean in the world and replaces its closed shoreline
	// spline with the largest closed Z=0 contour extracted from the final signed DEM.
	static bool UpdateOceanFromZeroContour(
		const FTerrainHeightField& HeightField,
		const FTransform& TerrainTransform,
		UWorld* World);
};
