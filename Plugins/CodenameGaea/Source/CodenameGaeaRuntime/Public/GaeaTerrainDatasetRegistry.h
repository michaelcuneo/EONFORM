#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"

struct CODENAMEGAEARUNTIME_API FGaeaTerrainDatasetMetadata
{
	/** Physical centimetres represented by a normalized Height value of 1.0 before Output scaling. */
	float HeightScale = 1000.0f;

	/** EONFORM's terrain datum is invariant: normalized height 0.0 is world sea level Z = 0. */
	float SeaLevelNormalized = 0.0f;
};

/** Cached statistics for the canonical Height field. Values are in authored field units (normally normalized -1..1). */
struct CODENAMEGAEARUNTIME_API FGaeaTerrainHeightStatistics
{
	FIntPoint Resolution = FIntPoint::ZeroValue;
	uint64 SampleCount = 0;
	uint64 LandSampleCount = 0;
	uint64 UnderwaterSampleCount = 0;
	uint64 SeaLevelSampleCount = 0;
	double Minimum = 0.0;
	double Maximum = 0.0;
	double Mean = 0.0;

	bool IsValid() const
	{
		return Resolution.X > 1 && Resolution.Y > 1 && SampleCount > 0;
	}

	bool CrossesSeaLevel() const
	{
		return LandSampleCount > 0 && UnderwaterSampleCount > 0;
	}

	double GetLandFraction() const
	{
		return SampleCount > 0 ? static_cast<double>(LandSampleCount) / static_cast<double>(SampleCount) : 0.0;
	}

	double GetUnderwaterFraction() const
	{
		return SampleCount > 0 ? static_cast<double>(UnderwaterSampleCount) / static_cast<double>(SampleCount) : 0.0;
	}
};

struct CODENAMEGAEARUNTIME_API FGaeaTerrainDatasetSnapshot
{
	FName SourceId = NAME_None;
	uint64 Revision = 0;
	FGaeaTerrainDatasetMetadata Metadata;
	FGaeaTerrainDataset Dataset;

	bool IsValid() const
	{
		return !SourceId.IsNone()
			&& Revision > 0
			&& Metadata.HeightScale > UE_SMALL_NUMBER
			&& FMath::IsNearlyZero(Metadata.SeaLevelNormalized)
			&& !Dataset.IsEmpty();
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
	static uint64 Publish(
		FName SourceId,
		const FGaeaTerrainDataset& Dataset,
		const FGaeaTerrainDatasetMetadata& Metadata = FGaeaTerrainDatasetMetadata());

	static uint64 Publish(
		FName SourceId,
		FGaeaTerrainDataset&& Dataset,
		const FGaeaTerrainDatasetMetadata& Metadata = FGaeaTerrainDatasetMetadata());

	static bool Get(FName SourceId, FGaeaTerrainDatasetSnapshot& OutSnapshot);
	static bool GetLatest(FGaeaTerrainDatasetSnapshot& OutSnapshot);

	/** Cheap summary query for UI/layout estimation without copying the full dataset. */
	static bool GetHeightResolution(FName SourceId, FIntPoint& OutResolution, uint64* OutRevision = nullptr);

	/** Cached at publish time; safe for frequent editor UI diagnostics. */
	static bool GetHeightStatistics(FName SourceId, FGaeaTerrainHeightStatistics& OutStatistics, uint64* OutRevision = nullptr);

	static bool Remove(FName SourceId);
	static void Reset();
};
