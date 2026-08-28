#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

namespace EonformTerrace
{
	bool ApplyNormalized(
		const FEonformScalarField& Source01,
		int32 TerraceCount,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FEonformScalarField& OutField01,
		FString* OutError = nullptr);
}

void RegisterEonformTerraceNode();
