#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainMeshMaterializer.h"

class AActor;
class UClass;
class UWorld;

struct FGaeaMeshTerrainOutputSettings
{
	/** 1.0 preserves EONFORM's authored world dimensions. */
	double HorizontalScale = 1.0;

	/** 1.0 preserves EONFORM's authored elevation. */
	double VerticalScale = 1.0;

	/** Zero means use the evaluated Height field resolution. */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;

	/** Number of independently provided base mesh regions. Adjacent regions share seam samples. */
	FIntPoint Sections = FIntPoint(1, 1);

	/** Native UE 5.8 Mesh Partition Definition asset. */
	TObjectPtr<UObject> MeshPartitionDefinition = nullptr;

	/** Optional existing Mesh Partition actor to update. */
	TObjectPtr<AActor> TargetMeshPartition = nullptr;
};

struct FGaeaMeshTerrainBuildResult
{
	bool bSuccess = false;
	FString Message;
	TObjectPtr<AActor> TerrainActor = nullptr;
	int32 VertexCount = 0;
	int32 TriangleCount = 0;
	int32 SectionCount = 0;
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
		const FGaeaMeshTerrainOutputSettings& Settings = FGaeaMeshTerrainOutputSettings());

	/** Used by the editor asset picker without exposing MeshPartition headers to CodenameGaeaEditor. */
	static UClass* GetMeshPartitionDefinitionClass();
};
