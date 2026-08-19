#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"

struct FTerrainContextMaps;
struct FTerrainGeologyMaps;
struct FTerrainHeightField;
struct FTerrainProcessMasks;

class FTerrainDatasetBridge
{
public:
	static FGaeaTerrainDataset Build(
		const FTerrainHeightField& HeightField,
		const FTerrainContextMaps* Context = nullptr,
		const FTerrainProcessMasks* ProcessMasks = nullptr,
		const FTerrainGeologyMaps* Geology = nullptr);
};
