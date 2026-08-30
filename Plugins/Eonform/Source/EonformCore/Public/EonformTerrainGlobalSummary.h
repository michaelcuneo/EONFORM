#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainEvaluator.h"

/**
 * Exact whole-world reductions over a graph output without materializing the
 * complete world raster. The reducer evaluates the requested upstream branch in
 * bounded full-width strips and shares the result through GlobalSummaryCache.
 */
class EONFORMCORE_API FEonformTerrainGlobalSummary
{
public:
	/**
	 * Resolves the exact minimum and maximum of a Terrain Height or ScalarField
	 * output over the full reference lattice represented by Context.
	 */
	static bool ResolveOutputRange(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context,
		const FGuid& NodeId,
		FName OutputName,
		float& OutMinimum,
		float& OutMaximum,
		FString* OutError = nullptr);
};
