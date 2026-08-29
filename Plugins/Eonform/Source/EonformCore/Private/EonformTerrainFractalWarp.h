#pragma once

#include "EonformTerrainProceduralOps.h"

namespace EonformTerrainProceduralOps
{
	/**
	 * Authoritative FractalWarp edge/bilinear sampler. The callback is evaluated
	 * only at resolved full-world integer lattice coordinates.
	 */
	float FractalWarpSampleBilinear(
		const TFunctionRef<float(int32, int32)>& SampleInteger,
		const FIntPoint& Dimensions,
		float X,
		float Y,
		EEdgeBehaviour EdgeBehaviour);

	/**
	 * Resolve the exact source coordinate used by Vector Field FractalWarp for
	 * one full-world lattice sample. This is the authoritative coordinate path
	 * used by both raster and streamed evaluation.
	 *
	 * The current point contract intentionally accepts only the source-independent
	 * Virtual path (ZScale == 0 and no Modulator), which is the recovered Ridge
	 * dependency chain. Other modes continue through FractalWarpFidelity until
	 * they receive an equivalent point contract.
	 */
	bool FractalWarpVectorCoordinate(
		const FVector2D& LatticeCoordinate,
		const FIntPoint& ReferenceDimensions,
		const FFractalWarpSettings& Settings,
		FVector2D& OutSourceCoordinate,
		FString* OutError = nullptr);

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
