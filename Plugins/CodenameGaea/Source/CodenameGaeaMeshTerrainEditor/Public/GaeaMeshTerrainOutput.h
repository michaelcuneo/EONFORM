#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainMeshMaterializer.h"

class AActor;
class UClass;
class UWorld;

enum class EGaeaMeshTerrainSectionLayout : uint8
{
	Automatic,
	Explicit
};

enum class EGaeaMeshTerrainSectionComplexity : uint8
{
	Responsive,
	Balanced,
	Detailed,
	Maximum
};

struct FGaeaMeshTerrainOutputSettings
{
	/** Legacy uniform XY multiplier. New physical output uses HorizontalScaleXY. */
	double HorizontalScale = 1.0;

	/** Independent X/Y physical scale multipliers. */
	FVector2d HorizontalScaleXY = FVector2d(1.0, 1.0);

	/** 1.0 preserves EONFORM's authored elevation. */
	double VerticalScale = 1.0;

	/** Zero means use the evaluated Height field resolution. */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;

	/** Automatic is the safe default; Explicit exposes Sections directly. */
	EGaeaMeshTerrainSectionLayout SectionLayout = EGaeaMeshTerrainSectionLayout::Automatic;

	/** Target base-region complexity used when SectionLayout is Automatic. */
	EGaeaMeshTerrainSectionComplexity SectionComplexity = EGaeaMeshTerrainSectionComplexity::Balanced;

	/** Explicit base-region layout. Ignored when SectionLayout is Automatic. */
	FIntPoint Sections = FIntPoint(1, 1);

	/** Native UE 5.8 Mesh Partition Definition asset. */
	TObjectPtr<UObject> MeshPartitionDefinition = nullptr;

	/** Optional existing Mesh Partition actor to update. */
	TObjectPtr<AActor> TargetMeshPartition = nullptr;
};

struct FGaeaMeshTerrainLayoutEstimate
{
	bool bValid = false;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	FIntPoint Sections = FIntPoint(1, 1);
	FIntPoint MaxSectionResolution = FIntPoint::ZeroValue;
	int64 SectionCount = 0;
	int64 TotalVertexCount = 0;
	int64 TotalTriangleCount = 0;
	int64 MaxSectionVertexCount = 0;
	int64 MaxSectionTriangleCount = 0;
	int32 TargetTrianglesPerSection = 0;
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

	/** Resolve automatic/explicit base-region layout and complexity estimates for a known sample resolution. */
	static FGaeaMeshTerrainLayoutEstimate EstimateLayout(
		const FIntPoint& Resolution,
		const FGaeaMeshTerrainOutputSettings& Settings = FGaeaMeshTerrainOutputSettings());

	/** Triangle budget represented by an automatic section-complexity preset. */
	static int32 GetTargetTrianglesPerSection(EGaeaMeshTerrainSectionComplexity Complexity);

	/** Used by the editor asset picker without exposing MeshPartition headers to CodenameGaeaEditor. */
	static UClass* GetMeshPartitionDefinitionClass();
};
