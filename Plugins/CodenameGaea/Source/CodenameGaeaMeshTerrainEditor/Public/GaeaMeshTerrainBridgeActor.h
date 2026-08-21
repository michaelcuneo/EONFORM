#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"
#include "GaeaMeshTerrainBridgeActor.generated.h"

class UGaeaTerrainGraphAsset;

/**
 * Editor-only vertical bridge from an EONFORM terrain graph to UE 5.8 Mesh Terrain.
 *
 * The graph is evaluated by CodenameGaeaCore, materialized to FDynamicMesh3 by
 * CodenameGaeaRuntime, then supplied directly to Mesh Terrain through Epic's
 * UMeshProviderModifier base layer.
 */
UCLASS(DisplayName="EONFORM Mesh Terrain Bridge")
class CODENAMEGAEAMESHTERRAINEDITOR_API AGaeaMeshTerrainBridgeActor : public AActor
{
	GENERATED_BODY()

public:
	AGaeaMeshTerrainBridgeActor();

	UFUNCTION(CallInEditor, Category="EONFORM|Mesh Terrain", meta=(DisplayName="Build Mesh Terrain"))
	void BuildMeshTerrain();

	UFUNCTION(CallInEditor, Category="EONFORM|Mesh Terrain", meta=(DisplayName="Clear Mesh Terrain"))
	void ClearMeshTerrain();

	UPROPERTY(EditAnywhere, Category="EONFORM|Source")
	TObjectPtr<UGaeaTerrainGraphAsset> GraphAsset;

	UPROPERTY(EditAnywhere, Category="EONFORM|Source")
	FName SourceDatasetName = NAME_None;

	UPROPERTY(EditAnywhere, Category="EONFORM|Source", meta=(ClampMin="0.001"))
	float DefaultHeightScale = 100000.0f;

	UPROPERTY(EditAnywhere, Category="EONFORM|Mesh Terrain")
	TObjectPtr<UMeshPartitionDefinition> MeshPartitionDefinition;

	UPROPERTY(EditAnywhere, Category="EONFORM|Mesh Terrain")
	TObjectPtr<AMeshPartition> TargetMeshPartition;

	UPROPERTY(VisibleAnywhere, Category="EONFORM|Mesh Terrain")
	TObjectPtr<UMeshProviderModifier> MeshProvider;

	UPROPERTY(VisibleAnywhere, Category="EONFORM|Status")
	FString LastBuildStatus;

private:
	AMeshPartition* ResolveOrCreateMeshPartition();
	void SetStatus(const FString& Message, bool bError);
};
