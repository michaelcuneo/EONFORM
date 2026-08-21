#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainValue.h"

struct CODENAMEGAEACORE_API FGaeaTerrainEvaluationContext
{
	FGaeaTerrainDataset SourceDataset;
	float HeightScale = 1000.0f;

	/** Optional physical world contract supplied by EONFORM's graph asset/output settings. */
	FGaeaTerrainPhysicalMetrics PhysicalMetrics = FGaeaTerrainPhysicalContext::GetActive();
};

struct CODENAMEGAEACORE_API FGaeaTerrainEvaluationResult
{
	bool bSuccess = false;
	FString Error;
	uint32 RecipeHash = 0;
	float HeightScale = 1000.0f;
	FGaeaTerrainDataset Dataset;
};

struct CODENAMEGAEACORE_API FGaeaTerrainNodeEvaluation
{
	TMap<FName, FGaeaTerrainValue> Outputs;

	const FGaeaTerrainValue* FindOutput(FName Name) const
	{
		// Gaea-facing terrain nodes use "Out". Older built-in nodes still publish
		// their terrain value under "Terrain", so final-output resolution accepts
		// both without forcing new nodes to expose the wrong public pin name.
		if (Name == TEXT("Terrain"))
		{
			if (const FGaeaTerrainValue* OutValue = Outputs.Find(TEXT("Out")))
			{
				return OutValue;
			}
		}
		return Outputs.Find(Name);
	}
};

using FGaeaTerrainNodeInputs = TMap<FName, const FGaeaTerrainValue*>;

using FGaeaTerrainNodeEvaluator = TFunction<bool(
	const FGaeaTerrainNode&,
	const FGaeaTerrainNodeInputs&,
	const FGaeaTerrainEvaluationContext&,
	FGaeaTerrainNodeEvaluation&,
	FString&)>;

/** Runtime registry for pure graph node evaluators. */
class CODENAMEGAEACORE_API FGaeaTerrainNodeRegistry
{
public:
	static void Register(FName NodeType, FGaeaTerrainNodeEvaluator Evaluator);
	static bool IsRegistered(FName NodeType);
	static void RegisterBuiltIns();
	static void Reset();

private:
	friend class FGaeaTerrainEvaluator;
	static const FGaeaTerrainNodeEvaluator* Find(FName NodeType);
};

/** Evaluates FGaeaTerrainRecipe without any editor-only dependency. */
class CODENAMEGAEACORE_API FGaeaTerrainEvaluator
{
public:
	static FGaeaTerrainEvaluationResult Evaluate(
		const FGaeaTerrainRecipe& Recipe,
		const FGaeaTerrainEvaluationContext& Context);
};
