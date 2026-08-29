#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainProceduralOps.h"

namespace EonformTerrainRawNoise
{
	/** Evaluate the exact raw Voronoi implementation at one virtual full-world sample coordinate. */
	float SampleVoronoiReference(
		const FVector2d& ReferenceCoordinate,
		int32 ReferenceResolutionX,
		const EonformTerrainProceduralOps::FVoronoiSettings& Settings);

	/** Evaluate the exact raw Perlin implementation at one virtual full-world sample coordinate. */
	float SamplePerlinReference(
		const FVector2d& ReferenceCoordinate,
		int32 ReferenceResolutionX,
		const EonformTerrainProceduralOps::FPerlinSettings& Settings);

	bool Voronoi(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FVoronoiSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr,
		const FEonformGridDomain* ReferenceDomain = nullptr);

	bool Perlin(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FPerlinSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr,
		const FEonformGridDomain* ReferenceDomain = nullptr);
}
