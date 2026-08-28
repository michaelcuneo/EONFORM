#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainProceduralOps.h"

namespace EonformVoronoi
{
	bool Generate(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FVoronoiSettings& Settings,
		float ClampValue,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}

void RegisterEonformVoronoiNode();
