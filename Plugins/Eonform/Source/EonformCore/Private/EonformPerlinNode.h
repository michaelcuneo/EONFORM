#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainProceduralOps.h"

namespace EonformPerlin
{
	bool Generate(
		const FEonformGridDomain& Domain,
		const EonformTerrainProceduralOps::FPerlinSettings& Settings,
		float HeightAmount,
		FEonformScalarField& OutField,
		FString* OutError = nullptr,
		const FEonformGridDomain* ReferenceDomain = nullptr);
}

void RegisterEonformPerlinNode();
