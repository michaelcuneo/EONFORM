#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainPhysicalMetrics.h"

struct CODENAMEGAEACORE_API FGaeaMountainLandformSettings
{
	int32 Resolution = 513;
	float WorldSize = 100000.0f;
	float HeightScale = 8000.0f;
	float Radius = 0.72f;
	float Elongation = 0.52f;
	float OrientationDegrees = 20.0f;
	float PeakSharpness = 0.58f;
	float RidgeStrength = 0.72f;
	float RidgeFrequency = 4.0f;
	float Roughness = 0.34f;
	float Asymmetry = 0.18f;
	float BaseElevation = 0.02f;
	int32 Seed = 1337;
};

struct CODENAMEGAEACORE_API FGaeaMountainLandformResult
{
	FGaeaTerrainDataset Dataset;
	float HeightScale = 8000.0f;
};

/** Reusable high-level landform construction used by public Terrain nodes and composites. */
class CODENAMEGAEACORE_API FGaeaTerrainLandformOps
{
public:
	static bool BuildMountain(
		const FGaeaMountainLandformSettings& Settings,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FGaeaMountainLandformResult& OutResult,
		FString* OutError = nullptr);
};
