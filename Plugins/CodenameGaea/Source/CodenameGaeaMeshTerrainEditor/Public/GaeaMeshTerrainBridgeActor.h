#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GaeaMeshTerrainBridgeActor.generated.h"

class UGaeaTerrainGraphAsset;
class UPrimitiveComponent;
class USceneComponent;
class UArrowComponent;

/**
 * Editor-only vertical bridge from an EONFORM terrain graph to UE 5.8 Mesh Terrain.
 *
 * MeshPartition is intentionally kept out of this reflected public header.
 * The experimental UE 5.8 API is isolated to the implementation file so UHT
 * and the rest of EONFORM do not inherit MeshPartition's editor headers.
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

	/** Optional Mesh Partition Definition asset. Kept as UObject to isolate the experimental type from UHT. */
	UPROPERTY(EditAnywhere, Category="EONFORM|Mesh Terrain")
	TObjectPtr<UObject> MeshPartitionDefinition;

	/** Optional existing Mesh Partition actor. Kept as AActor to isolate the experimental type from UHT. */
	UPROPERTY(EditInstanceOnly, Category="EONFORM|Mesh Terrain")
	TObjectPtr<AActor> TargetMeshPartition;

	/** Ordinary scene root so the bridge behaves like a normal selectable level actor. */
	UPROPERTY(VisibleAnywhere, Category="EONFORM|Bridge")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visible editor marker used to select and locate the bridge in the viewport. */
	UPROPERTY(VisibleAnywhere, Category="EONFORM|Bridge")
	TObjectPtr<UArrowComponent> EditorMarker;

	UPROPERTY(VisibleAnywhere, Category="EONFORM|Mesh Terrain")
	TObjectPtr<UPrimitiveComponent> MeshProviderComponent;

	UPROPERTY(VisibleAnywhere, Category="EONFORM|Status")
	FString LastBuildStatus;

private:
	AActor* ResolveOrCreateMeshPartitionActor();
	void SetStatus(const FString& Message, bool bError);
};
