#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainProceduralOps.h"

namespace GaeaTerrainRawNoise
{
	bool Voronoi(
		const FGaeaGridDomain& Domain,
		const GaeaTerrainProceduralOps::FVoronoiSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);

	bool Perlin(
		const FGaeaGridDomain& Domain,
		const GaeaTerrainProceduralOps::FPerlinSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);
}
