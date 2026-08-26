#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainEvaluator.h"

namespace EonformTerrainDomainScaling
{
	/** Resample a scalar field over the full normalized map extent into TargetDomain. */
	bool ResampleScalarField(
		const FEonformScalarField& Source,
		const FEonformGridDomain& TargetDomain,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	/** Resample a graph value while preserving terrain field descriptors and provenance. */
	bool ResampleValue(
		const FEonformTerrainValue& Source,
		const FEonformGridDomain& TargetDomain,
		FEonformTerrainValue& OutValue,
		FString* OutError = nullptr);

	/**
	 * Give every input of one node a single compatible grid domain.
	 * Explicit output resolution/physical metrics win. In native mode the first
	 * connected grid value becomes the node-local canonical domain.
	 */
	bool NormalizeInputs(
		const FEonformTerrainNodeInputs& RawInputs,
		const FEonformTerrainEvaluationContext& Context,
		TMap<FName, FEonformTerrainValue>& OutOwnedInputs,
		FEonformTerrainNodeInputs& OutInputs,
		FString* OutError = nullptr);

	/**
	 * Enforce the selected Terrain Output domain on every graph-facing node output.
	 * Nodes may use a cheaper/native internal working resolution, but downstream
	 * nodes always receive the selected output resolution and physical extent.
	 */
	bool NormalizeOutputs(
		FEonformTerrainNodeEvaluation& InOutEvaluation,
		const FEonformTerrainEvaluationContext& Context,
		FString* OutError = nullptr);
}
