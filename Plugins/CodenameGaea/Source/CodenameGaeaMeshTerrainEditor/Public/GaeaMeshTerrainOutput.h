#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"

class AActor;
class UWorld;

struct FGaeaMeshTerrainBuildResult
{
	bool bSuccess = false;
	FString Message;
	TObjectPtr<AActor> TerrainActor = nullptr;
	int32 VertexCount = 0;
	int32 TriangleCount = 0;
};

/**
 * Editor-only output backend that publishes an evaluated EONFORM terrain dataset
 * directly into UE 5.8 Mesh Terrain while keeping Epic's experimental types
 * isolated inside CodenameGaeaMeshTerrainEditor.
 */
class CODENAMEGAEAMESHTERRAINEDITOR_API FGaeaMeshTerrainOutput
{
public:
	static FGaeaMeshTerrainBuildResult Build(
		UWorld* World,
		const FGaeaTerrainDataset& Dataset,
		float HeightScale,
		UObject* PreferredMeshPartitionDefinition = nullptr,
		AActor* PreferredMeshPartition = nullptr);
};
