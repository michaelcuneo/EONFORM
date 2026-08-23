#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainPhysicalMetrics.h"

/** Internal settings for the Gaea-compatible Mountain primitive. */
struct CODENAMEGAEACORE_API FGaeaMountainLandformSettings
{
	// Domain fallbacks. The public node intentionally mirrors Gaea's visible controls.
	int32 Resolution = 1009;
	float WorldSize = 100000.0f;
	float HeightScale = 300000.0f;

	// Gaea Mountain public contract.
	float Scale = 1.0f;
	float Height = 0.92f;
	FName Style = TEXT("Basic");
	FName Bulk = TEXT("Medium");
	bool bReduceDetails = false;
	int32 Seed = 1337;
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
};

struct CODENAMEGAEACORE_API FGaeaMountainLandformResult
{
	FGaeaTerrainDataset Dataset;
	float HeightScale = 300000.0f;
};

/** Reusable high-level landform construction used by public Terrain nodes and composites. */
class CODENAMEGAEACORE_API FGaeaTerrainLandformOps
{
public:
	/**
	 * Builds the pre-simulation Mountain body. The primitive contains a dominant
	 * summit complex, recursive ridge branches, seeded valleys, warped ridged
	 * multifractal structure, and only subtle Voronoi modulation. Hydraulic and
	 * thermal processes are applied by the public Mountain composite afterwards.
	 */
	static bool BuildMountain(
		const FGaeaMountainLandformSettings& Settings,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FGaeaMountainLandformResult& OutResult,
		FString* OutError = nullptr);
};
