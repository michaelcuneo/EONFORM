#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainDrainageMaps;
struct FTerrainStructuralMaps;

struct FTerrainPhysiographySettings
{
	// Width of the broad regional surface used to distinguish uplands, hillslopes
	// and lowlands. This is a landform scale, not a detail/noise scale.
	float RegionalScaleCm = 8000.0f;

	float LowlandStrength = 0.6f;
	float LowlandBroadScale = 0.58f;
	float LowlandResidualScale = 0.18f;
	float RollingStrength = 0.08f;

	// Valley geometry is driven by contributing drainage area. Small drainage
	// areas form gullies; large drainage areas progressively widen into valleys.
	float ValleyStrength = 0.7f;
	float ValleyWidthCm = 1800.0f;
	float ValleyDepthCm = 500.0f;
	float ValleyProfile = 1.35f;

	// Subdued fluvial/structural benches on suitable valley shoulders.
	float BenchStrength = 0.24f;
	float BenchHeightCm = 220.0f;

	// Depression bottoms may accumulate material without filling the rendered DEM
	// all the way to the routing spill surface.
	float BasinFloorStrength = 0.3f;
};

struct FTerrainPhysiographyMaps
{
	TArray<float> Upland;
	TArray<float> Hillslope;
	TArray<float> Lowland;
	TArray<float> Valley;
	TArray<float> ValleyFloor;
	TArray<float> Bench;
	TArray<float> Basin;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& Upland.Num() == NumCells
			&& Hillslope.Num() == NumCells
			&& Lowland.Num() == NumCells
			&& Valley.Num() == NumCells
			&& ValleyFloor.Num() == NumCells
			&& Bench.Num() == NumCells
			&& Basin.Num() == NumCells;
	}
};

class FTerrainPhysiography
{
public:
	// Builds broad landform hierarchy on the already-signed DEM. Ocean geometry is
	// untouched and positive land samples are never allowed to cross sea level.
	static bool Apply(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainDrainageMaps& Drainage,
		const FTerrainStructuralMaps* Structure,
		const FTerrainPhysiographySettings& Settings,
		FTerrainPhysiographyMaps& OutMaps);
};
