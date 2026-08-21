#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GaeaTerrainDataset.h"

/** Geometry controls applied when materializing an EONFORM terrain dataset. */
struct CODENAMEGAEARUNTIME_API FGaeaTerrainMeshBuildOptions
{
	/** Existing EONFORM vertical conversion factor. */
	float HeightScale = 1000.0f;

	/** 1.0 preserves the authored real-world XY size. Baked into vertices, not actor scale. */
	double HorizontalScale = 1.0;

	/** 1.0 preserves the authored elevation scale. Applied in addition to HeightScale. */
	double VerticalScale = 1.0;

	/** Optional output sample resolution. Zero components mean use the source Height field resolution. */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;
};

/** Converts a terrain dataset Height field into a regular-grid FDynamicMesh3. */
class CODENAMEGAEARUNTIME_API FGaeaTerrainMeshMaterializer
{
public:
	static bool BuildDynamicMesh(
		const FGaeaTerrainDataset& Dataset,
		float HeightScale,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);

	static bool BuildDynamicMesh(
		const FGaeaTerrainDataset& Dataset,
		const FGaeaTerrainMeshBuildOptions& Options,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);

	/**
	 * Builds one region of the target output grid. Start/End are inclusive sample coordinates
	 * in the resolved target resolution, allowing adjacent Mesh Terrain sections to share seams.
	 */
	static bool BuildDynamicMeshRegion(
		const FGaeaTerrainDataset& Dataset,
		const FGaeaTerrainMeshBuildOptions& Options,
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);

	static FIntPoint ResolveTargetResolution(
		const FGaeaTerrainDataset& Dataset,
		const FGaeaTerrainMeshBuildOptions& Options);
};
