#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

namespace EonformTerrainProceduralOps
{
	// Direct port of QuadSpinner.Gaea.Nodes.Profiles.Terrace for the
	// forceZero=false path used by Gaea 2.3.0.1 Landscapes.Ridge.
	bool TerraceFidelity(
		const FEonformScalarField& Source,
		int32 NumTerraces,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}
