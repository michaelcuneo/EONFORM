#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainContext.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainGeology.h"

struct CODENAMEGAEACORE_API FGaeaTerrainDerivedDataSettings
{
	int32 GeologySeed = 1337;
	FGaeaTerrainGeologySettings Geology;
	FGaeaTerrainProcessMaskSettings ProcessMasks;
};

/**
 * Supplies analysis fields on demand without forcing analysis nodes into the
 * main terrain-processing chain. Existing complete field groups are preserved,
 * so explicitly authored analysis nodes act as overrides.
 */
class CODENAMEGAEACORE_API FGaeaTerrainDerivedData
{
public:
	static bool EnsureContext(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureGeology(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainDerivedDataSettings& Settings = FGaeaTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	static bool EnsureProcessMasks(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainDerivedDataSettings& Settings = FGaeaTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	static bool EnsureHydraulicInputs(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainDerivedDataSettings& Settings = FGaeaTerrainDerivedDataSettings(),
		FString* OutError = nullptr);
};
