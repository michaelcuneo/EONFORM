#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GaeaTerrainDataset.h"

/** Converts a terrain dataset Height field into a regular-grid FDynamicMesh3. */
class CODENAMEGAEARUNTIME_API FGaeaTerrainMeshMaterializer
{
public:
	static bool BuildDynamicMesh(
		const FGaeaTerrainDataset& Dataset,
		float HeightScale,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FString* OutError = nullptr);
};
