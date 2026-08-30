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
	double HorizontalScale = 1.0;
	FVector2d HorizontalScaleXY = FVector2d(1.0, 1.0);
	double VerticalScale = 1.0;
	FIntPoint TargetResolution = FIntPoint::ZeroValue;
	EEonformMeshTerrainSectionLayout SectionLayout = EEonformMeshTerrainSectionLayout::Automatic;
	EEonformMeshTerrainSectionComplexity SectionComplexity = EEonformMeshTerrainSectionComplexity::Balanced;
	FIntPoint Sections = FIntPoint(1, 1);
	TObjectPtr<UObject> MeshPartitionDefinition = nullptr;
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

class EONFORMMESHTERRAINEDITOR_API FEonformMeshTerrainOutput
{
public:
	static FEonformMeshTerrainBuildResult Build(
		UWorld* World,
		const FEonformTerrainDataset& Dataset,
		float HeightScale,
		const FEonformMeshTerrainOutputSettings& Settings = FEonformMeshTerrainOutputSettings());

	/** Re-evaluate and replace only the requested base-region coordinates. */
	static FEonformMeshTerrainBuildResult BuildRegions(
		UWorld* World,
		const FEonformTerrainDataset& Dataset,
		float HeightScale,
		const FEonformMeshTerrainOutputSettings& Settings,
		const TArray<FIntPoint>& RegionIndices);

	/** Remove only the requested Mesh Terrain base-region materialisations. */
	static int32 UnloadRegions(
		UWorld* World,
		const TArray<FIntPoint>& RegionIndices,
		FString* OutError = nullptr);

	/** Find the materialised actor for one deterministic base-region coordinate. */
	static AActor* FindRegionActor(UWorld* World, const FIntPoint& RegionIndex);

	/** Resolve a deterministic base-region coordinate from an EONFORM region actor. */
	static bool TryGetRegionIndex(const AActor* Actor, FIntPoint& OutRegionIndex);

	static FEonformMeshTerrainLayoutEstimate EstimateLayout(
		const FIntPoint& Resolution,
		const FEonformMeshTerrainOutputSettings& Settings = FEonformMeshTerrainOutputSettings());

	static int32 GetTargetTrianglesPerSection(EEonformMeshTerrainSectionComplexity Complexity);
	static UClass* GetMeshPartitionDefinitionClass();
};
