#pragma once

#include "EonformTerrainProceduralOps.h"

namespace EonformTerrainProceduralOps
{
	/**
	 * Independent implementation of the observed FractalWarp contract.
	 *
	 * Important invariants:
	 * - noise frequency is 1 / (Size * Resolution)
	 * - displacement is Strength * Resolution * (PersistStrength ? Size : 1)
	 * - Perlin FBM does not receive an additional Perturbation pass
	 * - modulation is a directional coordinate offset centred on 0.5
	 * - Vector Field mode composes coordinates and samples the source once
	 */
	bool FractalWarpFidelity(
		const FEonformScalarField& Source,
		const FFractalWarpSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}
