#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainRecipe.h"

struct CODENAMEGAEACORE_API FGaeaTerrainEvaluationContext
{
	FGaeaTerrainDataset SourceDataset;
	float HeightScale = 1000.0f;
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
	float HeightScale = 1000.0f;
	FGaeaTerrainDataset Dataset;
};

using FGaeaTerrainNodeEvaluator = TFunction<bool(
	const FGaeaTerrainNode&,
	const TMap<FName, const FGaeaTerrainNodeEvaluation*>&,
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
