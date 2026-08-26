#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainDataset.h"

struct FTerrainContextMaps;
struct FTerrainGeologyMaps;
struct FTerrainHeightField;
struct FTerrainHydraulicErosionResult;
struct FTerrainProcessMasks;

class FTerrainDatasetBridge
{
public:
	static FEonformTerrainDataset Build(
		const FTerrainHeightField& HeightField,
		const FTerrainContextMaps* Context = nullptr,
		const FTerrainProcessMasks* ProcessMasks = nullptr,
		const FTerrainGeologyMaps* Geology = nullptr,
		const FTerrainHydraulicErosionResult* HydraulicErosion = nullptr);
};
