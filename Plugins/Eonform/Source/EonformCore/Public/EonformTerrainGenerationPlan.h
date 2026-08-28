#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainRecipe.h"

struct EONFORMCORE_API FEonformTerrainGenerationPlan
{
	FEonformTerrainRecipe Recipe;
	FEonformTerrainEvaluationContext Context;
	uint64 Revision = 0;

	bool IsValid() const
	{
		return Revision != 0 && Recipe.Validate(nullptr);
	}
};

/**
 * Process-local handoff between the graph editor and terrain materialization.
 * Stores only the validated recipe/context, never the full final terrain raster.
 */
class EONFORMCORE_API FEonformTerrainGenerationPlanRegistry
{
public:
	static void Publish(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context,
		uint64 Revision);

	static bool Get(FEonformTerrainGenerationPlan& OutPlan);
	static void Reset();
};
