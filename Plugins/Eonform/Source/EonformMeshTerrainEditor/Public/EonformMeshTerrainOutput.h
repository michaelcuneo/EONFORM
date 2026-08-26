#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainMeshMaterializer.h"

class AActor;
class UClass;
class UWorld;

enum class EEonformMeshTerrainSectionLayout : uint8
{
	Automatic,
	Explicit
};

enum class EEonformMeshTerrainSectionComplexity : uint8
{
	Responsive,
	Balanced,
	Detailed,
	Maximum
};

struct FEonformMeshTerrainOutputSettings
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
	EEonformMeshTerrainSectionLayout SectionLayout = EEonformMeshTerrainSectionLayout::Automatic;

	/** Target base-region complexity used when SectionLayout is Automatic. */
	EEonformMeshTerrainSectionComplexity SectionComplexity = EEonformMeshTerrainSectionComplexity::Balanced;

	/** Explicit base-region layout. Ignored when SectionLayout is Automatic. */
	FIntPoint Sections = FIntPoint(1, 1);

	/** Native UE 5.8 Mesh Partition Definition asset. */
	TObjectPtr<UObject> MeshPartitionDefinition = nullptr;

	/** Optional existing Mesh Partition actor to update. */
	TObjectPtr<AActor> TargetMeshPartition = nullptr;
};

struct FEonformMeshTerrainLayoutEstimate
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

struct FEonformMeshTerrainBuildResult
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
 * isolated inside EonformMeshTerrainEditor.
 */
class EONFORMMESHTERRAINEDITOR_API FEonformMeshTerrainOutput
{
public:
	static FEonformMeshTerrainBuildResult Build(
		UWorld* World,
		const FEonformTerrainDataset& Dataset,
		float HeightScale,
		const FEonformMeshTerrainOutputSettings& Settings = FEonformMeshTerrainOutputSettings());

	/** Resolve automatic/explicit base-region layout and complexity estimates for a known sample resolution. */
	static FEonformMeshTerrainLayoutEstimate EstimateLayout(
		const FIntPoint& Resolution,
		const FEonformMeshTerrainOutputSettings& Settings = FEonformMeshTerrainOutputSettings());

	/** Triangle budget represented by an automatic section-complexity preset. */
	static int32 GetTargetTrianglesPerSection(EEonformMeshTerrainSectionComplexity Complexity);

	/** Used by the editor asset picker without exposing MeshPartition headers to EonformEditor. */
	static UClass* GetMeshPartitionDefinitionClass();
};
