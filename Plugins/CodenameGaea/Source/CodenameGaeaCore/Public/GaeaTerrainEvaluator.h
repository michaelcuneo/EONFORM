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

	/**
	 * Changes whenever an external part of the evaluation context changes.
	 * Incremental evaluation mixes this into node signatures so physical output
	 * settings or external source revisions cannot accidentally reuse stale data.
	 */
	uint64 CacheContextRevision = 0;
};

struct CODENAMEGAEACORE_API FGaeaTerrainEvaluationResult
{
	bool bSuccess = false;
	FString Error;
	uint32 RecipeHash = 0;
	float HeightScale = 1000.0f;
	FGaeaTerrainDataset Dataset;

	/** Incremental-evaluation diagnostics. Zero for the legacy stateless path. */
	int32 EvaluatedNodeCount = 0;
	int32 CachedNodeCount = 0;
	double EvaluationMilliseconds = 0.0;
};

struct CODENAMEGAEACORE_API FGaeaTerrainNodeEvaluation
{
	TMap<FName, FGaeaTerrainValue> Outputs;

	const FGaeaTerrainValue* FindOutput(FName Name) const
	{
		// Prefer an exact pin match first. Some Derive nodes expose a terrain
		// passthrough named "Terrain" and a scalar result named "Out".
		if (const FGaeaTerrainValue* Exact = Outputs.Find(Name))
		{
			return Exact;
		}

		// Compatibility fallback for modern Gaea-facing nodes whose terrain output
		// is named "Out" rather than "Terrain".
		if (Name == TEXT("Terrain"))
		{
			return Outputs.Find(TEXT("Out"));
		}
		return nullptr;
	}
};

struct CODENAMEGAEACORE_API FGaeaTerrainEvaluationCacheEntry
{
	uint32 Signature = 0;
	FGaeaTerrainNodeEvaluation Evaluation;
};

/**
 * Persistent graph-node cache used by interactive editors.
 *
 * A node is reusable only when its own parameters, incoming wiring, every
 * upstream node signature, and the external evaluation-context revision match.
 * Changing one node therefore dirties only that node and its downstream chain.
 */
struct CODENAMEGAEACORE_API FGaeaTerrainEvaluationCache
{
	TMap<FGuid, FGaeaTerrainEvaluationCacheEntry> Nodes;

	void Reset()
	{
		Nodes.Reset();
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
	/** Stateless full evaluation. Kept for runtime/tests and callers that do not own a cache. */
	static FGaeaTerrainEvaluationResult Evaluate(
		const FGaeaTerrainRecipe& Recipe,
		const FGaeaTerrainEvaluationContext& Context);

	/**
	 * Persistent incremental evaluation for interactive graph authoring.
	 * Unchanged upstream nodes are reused across calls; only dirty downstream
	 * work is recomputed.
	 */
	static FGaeaTerrainEvaluationResult EvaluateIncremental(
		const FGaeaTerrainRecipe& Recipe,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainEvaluationCache& Cache);
};
