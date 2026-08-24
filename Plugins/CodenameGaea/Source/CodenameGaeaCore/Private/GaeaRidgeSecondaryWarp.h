#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainProceduralOps.h"

namespace GaeaRidgeSecondaryWarp
{
	// Gaea 2.3.0.1 Warps.FractalWarp specialization used by Landscapes.Ridge:
	// VoronoiR Virtual pass followed by the source-mandated Perlin perturb pass.
	bool Apply(
		const FGaeaScalarField& Source,
		float Size,
		float Strength,
		float Perturbation,
		int32 Octaves,
		float Roughness,
		int32 Seed,
		float Jitter,
		GaeaTerrainProceduralOps::EEdgeBehaviour EdgeBehaviour,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);
}
