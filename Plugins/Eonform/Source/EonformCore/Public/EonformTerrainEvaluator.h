#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainValue.h"

struct EONFORMCORE_API FEonformTerrainEvaluationContext
{
	FEonformTerrainDataset SourceDataset;
	float HeightScale = 1000.0f;

	/** Optional physical world contract supplied by EONFORM's graph asset/output settings. */
	FEonformTerrainPhysicalMetrics PhysicalMetrics = FEonformTerrainPhysicalContext::GetActive();

	/**
	 * Preferred evaluation resolution for source/terrain nodes. Zero means the
	 * node may choose an appropriate native working resolution. Expensive
	 * composites may clamp very large requests until tiled evaluation is active.
	 */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;

	/**
	 * Changes whenever an external part of the evaluation context changes.
	 * Incremental evaluation mixes this into node signatures so physical output
	 * settings or external source revisions cannot accidentally reuse stale data.
	 */
	uint64 CacheContextRevision = 0;
};

struct EONFORMCORE_API FEonformTerrainEvaluationResult
{
	bool bSuccess = false;
	FString Error;
	uint32 RecipeHash = 0;
	float HeightScale = 1000.0f;
	FEonformTerrainDataset Dataset;

	/** Incremental-evaluation diagnostics. Zero for the legacy stateless path. */
	int32 EvaluatedNodeCount = 0;
	int32 CachedNodeCount = 0;
	double EvaluationMilliseconds = 0.0;
};

struct EONFORMCORE_API FEonformTerrainNodeEvaluation
{
	TMap<FName, FEonformTerrainValue> Outputs;

	const FEonformTerrainValue* FindOutput(FName Name) const
	{
		// Prefer an exact pin match first. Some Derive nodes expose a terrain
		// passthrough named "Terrain" and a scalar result named "Out".
		if (const FEonformTerrainValue* Exact = Outputs.Find(Name))
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

struct EONFORMCORE_API FEonformTerrainEvaluationCacheEntry
{
	uint32 Signature = 0;
	FEonformTerrainNodeEvaluation Evaluation;
};

/**
 * Persistent graph-node cache used by interactive editors.
 *
 * A node is reusable only when its own parameters, incoming wiring, every
 * upstream node signature, and the external evaluation-context revision match.
 * Changing one node therefore dirties only that node and its downstream chain.
 */
struct EONFORMCORE_API FEonformTerrainEvaluationCache
{
	TMap<FGuid, FEonformTerrainEvaluationCacheEntry> Nodes;

	void Reset()
	{
		Nodes.Reset();
	}
};

using FEonformTerrainNodeInputs = TMap<FName, const FEonformTerrainValue*>;

using FEonformTerrainNodeEvaluator = TFunction<bool(
	const FEonformTerrainNode&,
	const FEonformTerrainNodeInputs&,
	const FEonformTerrainEvaluationContext&,
	FEonformTerrainNodeEvaluation&,
	FString&)>;

/** Runtime registry for pure graph node evaluators. */
class EONFORMCORE_API FEonformTerrainNodeRegistry
{
public:
	static void Register(FName NodeType, FEonformTerrainNodeEvaluator Evaluator);
	static bool IsRegistered(FName NodeType);
	static void RegisterBuiltIns();
	static void Reset();

private:
	friend class FEonformTerrainEvaluator;
	static const FEonformTerrainNodeEvaluator* Find(FName NodeType);
};

/** Evaluates FEonformTerrainRecipe without any editor-only dependency. */
class EONFORMCORE_API FEonformTerrainEvaluator
{
public:
	/** Stateless full evaluation. Kept for runtime/tests and callers that do not own a cache. */
	static FEonformTerrainEvaluationResult Evaluate(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context);

	/**
	 * Persistent incremental evaluation for interactive graph authoring.
	 * Unchanged upstream nodes are reused across calls; only dirty downstream
	 * work is recomputed.
	 */
	static FEonformTerrainEvaluationResult EvaluateIncremental(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainEvaluationCache& Cache);
};
