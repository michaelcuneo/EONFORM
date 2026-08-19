#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"

struct CODENAMEGAEARUNTIME_API FGaeaTerrainDatasetSnapshot
{
	FName SourceId = NAME_None;
	uint64 Revision = 0;
	FGaeaTerrainDataset Dataset;

	bool IsValid() const
	{
		return !SourceId.IsNone() && Revision > 0 && !Dataset.IsEmpty();
	}
};

/**
 * Small runtime-safe handoff registry for generated terrain datasets.
 *
 * Producers publish immutable snapshots by source id. Editor tooling,
 * runtime materializers, and gameplay integration can consume copies without
 * depending directly on the producer implementation.
 */
class CODENAMEGAEARUNTIME_API FGaeaTerrainDatasetRegistry
{
public:
	static uint64 Publish(FName SourceId, const FGaeaTerrainDataset& Dataset);
	static uint64 Publish(FName SourceId, FGaeaTerrainDataset&& Dataset);

	static bool Get(FName SourceId, FGaeaTerrainDatasetSnapshot& OutSnapshot);
	static bool GetLatest(FGaeaTerrainDatasetSnapshot& OutSnapshot);

	static bool Remove(FName SourceId);
	static void Reset();
};
