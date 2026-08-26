#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainProceduralOps.h"

namespace EonformTerrainRawNoise
{
	bool Voronoi(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FVoronoiSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	bool Perlin(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FPerlinSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}
