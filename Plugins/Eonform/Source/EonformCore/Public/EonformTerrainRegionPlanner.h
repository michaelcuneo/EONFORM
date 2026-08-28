#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainEvaluator.h"

/** One deterministic section of a full EONFORM output grid. */
struct EONFORMCORE_API FEonformTerrainRegionRequest
{
	FIntPoint RegionIndex = FIntPoint::ZeroValue;
	FIntPoint StartSample = FIntPoint::ZeroValue;
	FIntPoint EndSample = FIntPoint::ZeroValue;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	FEonformTerrainEvaluationRegion EvaluationRegion;

	bool IsValid() const
	{
		return RegionIndex.X >= 0
			&& RegionIndex.Y >= 0
			&& StartSample.X >= 0
			&& StartSample.Y >= 0
			&& EndSample.X > StartSample.X
			&& EndSample.Y > StartSample.Y
			&& Resolution.X == EndSample.X - StartSample.X + 1
			&& Resolution.Y == EndSample.Y - StartSample.Y + 1
			&& EvaluationRegion.IsValid();
	}
};

/**
 * Splits a virtual full-world sample grid into deterministic overlapping-edge
 * requests suitable for independent evaluation and Mesh Terrain publication.
 * Adjacent regions share their boundary sample exactly; halos are evaluation
 * storage only and do not change ownership of the interior samples.
 */
class EONFORMCORE_API FEonformTerrainRegionPlanner
{
public:
	static bool BuildRequests(
		const FIntPoint& FullResolution,
		const FVector2d& WorldMinCm,
		const FVector2d& WorldMaxCm,
		const FIntPoint& Sections,
		int32 BorderSamples,
		TArray<FEonformTerrainRegionRequest>& OutRequests,
		FString* OutError = nullptr);
};
