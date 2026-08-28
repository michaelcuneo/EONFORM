#pragma once

#include "CoreMinimal.h"
#include "EonformMeshTerrainOutput.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainRecipe.h"

class UWorld;

/**
 * Evaluates a region-safe EONFORM recipe directly into UE 5.8 Mesh Terrain base
 * regions without ever materializing the complete final-resolution terrain.
 */
class EONFORMMESHTERRAINEDITOR_API FEonformMeshTerrainRegionalOutput
{
public:
	static FEonformMeshTerrainBuildResult Build(
		UWorld* World,
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& BaseContext,
		const FEonformMeshTerrainOutputSettings& Settings = FEonformMeshTerrainOutputSettings());
};
