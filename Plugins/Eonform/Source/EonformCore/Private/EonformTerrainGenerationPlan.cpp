#include "EonformTerrainGenerationPlan.h"

namespace
{
	FCriticalSection GenerationPlanMutex;
	FEonformTerrainGenerationPlan GenerationPlan;
}

void FEonformTerrainGenerationPlanRegistry::Publish(
	const FEonformTerrainRecipe& Recipe,
	const FEonformTerrainEvaluationContext& Context,
	uint64 Revision)
{
	FScopeLock Lock(&GenerationPlanMutex);
	GenerationPlan.Recipe = Recipe;
	GenerationPlan.Context = Context;
	GenerationPlan.Revision = Revision;
	if (!GenerationPlan.IsValid())
	{
		GenerationPlan = FEonformTerrainGenerationPlan();
	}
}

bool FEonformTerrainGenerationPlanRegistry::Get(FEonformTerrainGenerationPlan& OutPlan)
{
	FScopeLock Lock(&GenerationPlanMutex);
	if (!GenerationPlan.IsValid()) return false;
	OutPlan = GenerationPlan;
	return true;
}

void FEonformTerrainGenerationPlanRegistry::Reset()
{
	FScopeLock Lock(&GenerationPlanMutex);
	GenerationPlan = FEonformTerrainGenerationPlan();
}
