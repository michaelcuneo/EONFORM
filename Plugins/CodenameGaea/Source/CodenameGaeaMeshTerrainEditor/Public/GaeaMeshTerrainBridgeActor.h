#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GaeaMeshTerrainBridgeActor.generated.h"

class AMeshPartition;
class UGaeaTerrainGraphAsset;
class UMeshPartitionDefinition;
class UMeshProviderModifier;

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

	/** Evaluate GraphAsset and publish the resulting terrain as the base mesh of TargetMeshPartition. */
	UFUNCTION(CallInEditor, Category="EONFORM|Mesh Terrain", meta=(DisplayName="Build Mesh Terrain"))
	void BuildMeshTerrain();

	/** Remove the generated mesh from the provider while leaving the bridge and partition in place. */
	UFUNCTION(CallInEditor, Category="EONFORM|Mesh Terrain", meta=(DisplayName="Clear Mesh Terrain"))
	void ClearMeshTerrain();

	UPROPERTY(EditAnywhere, Category="EONFORM|Source")
	TObjectPtr<UGaeaTerrainGraphAsset> GraphAsset;

	/** Optional source dataset for graphs that begin with SourceDataset. Primitive-only graphs may leave this empty. */
	UPROPERTY(EditAnywhere, Category="EONFORM|Source")
	FName SourceDatasetName = NAME_None;

	/** Physical Z scale in Unreal units used when the graph does not override it. */
	UPROPERTY(EditAnywhere, Category="EONFORM|Source", meta=(ClampMin="0.001"))
	float DefaultHeightScale = 100000.0f;

	/** Shared UE Mesh Terrain definition. If unset, UE's default definition is used. */
	UPROPERTY(EditAnywhere, Category="EONFORM|Mesh Terrain")
	TObjectPtr<UMeshPartitionDefinition> MeshPartitionDefinition;

	/** Existing Mesh Partition to target. Leave empty to have EONFORM create one beside this bridge. */
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
