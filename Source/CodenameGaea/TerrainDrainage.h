#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainDrainageSettings
{
	// Tiny routing-only rise used to establish a strict downhill direction across
	// flats and depression-filled surfaces. This never modifies rendered terrain.
	float FlatEpsilonCm = 1.0f;

	// Minimum depression fill depth that is classified as a lake/depression basin.
	float LakeDepthThresholdCm = 25.0f;
};

struct FTerrainDrainageMaps
{
	// Hydrologically conditioned routing surface in normalized height-field units.
	// Exterior ocean cells are represented at the sea-level datum (0), not seabed Z.
	TArray<float> ConditionedHeight;

	// Downstream cell for every non-ocean cell. INDEX_NONE means no receiver.
	TArray<int32> Receiver;

	// Contributing cell count and physical contributing area for every cell.
	TArray<float> FlowAccumulation;
	TArray<float> DrainageAreaCm2;

	// Hydrological topology products.
	TArray<int32> WatershedId;
	TArray<uint8> StreamOrder;

	// Depression/ocean classifications.
	TArray<float> FillDepthCm;
	TArray<float> LakeMask;
	TArray<float> SpillPointMask;
	TArray<float> OceanOutletMask;
	TArray<float> ExteriorOceanMask;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& ConditionedHeight.Num() == NumCells
			&& Receiver.Num() == NumCells
			&& FlowAccumulation.Num() == NumCells
			&& DrainageAreaCm2.Num() == NumCells
			&& WatershedId.Num() == NumCells
			&& StreamOrder.Num() == NumCells
			&& FillDepthCm.Num() == NumCells
			&& LakeMask.Num() == NumCells
			&& SpillPointMask.Num() == NumCells
			&& OceanOutletMask.Num() == NumCells
			&& ExteriorOceanMask.Num() == NumCells;
	}
};

class FTerrainDrainage
{
public:
	// Builds one authoritative drainage graph from the final terrain geometry.
	// The rendered height field is read-only: depression conditioning exists only
	// in the routing surface so later systems can preserve lakes and spill points.
	static bool Build(
		const FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainDrainageSettings& Settings,
		FTerrainDrainageMaps& OutMaps);
};
