#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "EonformTerrainDataset.h"

/** Geometry controls applied when materializing an EONFORM terrain dataset. */
struct EONFORMRUNTIME_API FEonformTerrainMeshBuildOptions
{
	/** Existing EONFORM vertical conversion factor for normalized Height values. */
	float HeightScale = 1000.0f;

	/** Legacy uniform XY multiplier, preserved for compatibility. */
	double HorizontalScale = 1.0;

	/** Independent X/Y multipliers applied after HorizontalScale. */
	FVector2d HorizontalScaleXY = FVector2d(1.0, 1.0);

	/** 1.0 preserves the authored elevation scale. Applied in addition to HeightScale. */
	double VerticalScale = 1.0;

	/** Optional output sample resolution. Zero components mean use the source Height field resolution. */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;
};

/** Converts a terrain dataset Height field into a regular-grid FDynamicMesh3. */
class EONFORMRUNTIME_API FEonformTerrainMeshMaterializer
{
public:
	static bool BuildDynamicMesh(
		const FEonformTerrainDataset& Dataset,
		float HeightScale,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);

	static bool BuildDynamicMesh(
		const FEonformTerrainDataset& Dataset,
		const FEonformTerrainMeshBuildOptions& Options,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);

	/**
	 * Builds one region of the target output grid. Start/End are inclusive sample coordinates
	 * in the resolved target resolution, allowing adjacent Mesh Terrain sections to share seams.
	 */
	static bool BuildDynamicMeshRegion(
		const FEonformTerrainDataset& Dataset,
		const FEonformTerrainMeshBuildOptions& Options,
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);

	static FIntPoint ResolveTargetResolution(
		const FEonformTerrainDataset& Dataset,
		const FEonformTerrainMeshBuildOptions& Options);
};
