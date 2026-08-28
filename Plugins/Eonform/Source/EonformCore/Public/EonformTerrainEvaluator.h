#pragma once

#include "CoreMinimal.h"
#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainValue.h"

/**
 * A spatial window requested from the continuous EONFORM world.
 *
 * WorldMinCm/WorldMaxCm describe the interior output bounds in Unreal world
 * centimetres. BorderSamples adds a dependency halo around the region without
 * changing the requested interior resolution or the published world bounds.
 */
struct EONFORMCORE_API FEonformTerrainEvaluationRegion
{
	FVector2d WorldMinCm = FVector2d::ZeroVector;
	FVector2d WorldMaxCm = FVector2d::ZeroVector;
	int32 BorderSamples = 0;

	bool IsValid() const
	{
		return BorderSamples >= 0
			&& FMath::IsFinite(WorldMinCm.X)
			&& FMath::IsFinite(WorldMinCm.Y)
			&& FMath::IsFinite(WorldMaxCm.X)
			&& FMath::IsFinite(WorldMaxCm.Y)
			&& WorldMaxCm.X > WorldMinCm.X
			&& WorldMaxCm.Y > WorldMinCm.Y;
	}
};

struct EONFORMCORE_API FEonformTerrainEvaluationContext
{
	FEonformTerrainDataset SourceDataset;
	float HeightScale = 1000.0f;

	/** Optional physical world contract supplied by EONFORM's graph asset/output settings. */
	FEonformTerrainPhysicalMetrics PhysicalMetrics = FEonformTerrainPhysicalContext::GetActive();

	/**
	 * Preferred interior evaluation resolution for source/terrain nodes. Zero
	 * means the node may choose an appropriate native working resolution. During
	 * regional evaluation this is the resolution of the requested region, not
	 * the resolution of the complete world.
	 */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;

	/**
	 * Virtual full-world resolution used to preserve global procedural scale when
	 * only a region is evaluated. Zero preserves legacy behaviour by falling back
	 * to TargetResolution and then the node's native resolution.
	 */
	FIntPoint ReferenceResolution = FIntPoint::ZeroValue;

	/** Optional local world-space evaluation window. Invalid means evaluate the full world. */
	FEonformTerrainEvaluationRegion Region;

	/**
	 * Changes whenever an external part of the evaluation context changes.
	 * Incremental evaluation mixes this into node signatures so physical output
	 * settings or external source revisions cannot accidentally reuse stale data.
	 */
	uint64 CacheContextRevision = 0;

	bool HasRegion() const
	{
		return Region.IsValid();
	}

	/** Resolve the virtual complete-world domain used by coordinate-stable procedural sources. */
	FEonformGridDomain ResolveReferenceDomain(
		const FIntPoint& NativeResolution,
		const FVector2d& FallbackWorldMinCm,
		const FVector2d& FallbackWorldMaxCm) const
	{
		FIntPoint Dimensions = NativeResolution;
		if (ReferenceResolution.X > 1 && ReferenceResolution.Y > 1)
		{
			Dimensions = ReferenceResolution;
		}
		else if (TargetResolution.X > 1 && TargetResolution.Y > 1)
		{
			Dimensions = TargetResolution;
		}

		FVector2d WorldMin = FallbackWorldMinCm;
		FVector2d WorldMax = FallbackWorldMaxCm;
		if (PhysicalMetrics.HasWorldDimensions())
		{
			const double WidthCm = PhysicalMetrics.WorldWidthMeters * 100.0;
			const double DepthCm = PhysicalMetrics.WorldDepthMeters * 100.0;
			WorldMin = FVector2d(-WidthCm * 0.5, -DepthCm * 0.5);
			WorldMax = FVector2d(WidthCm * 0.5, DepthCm * 0.5);
		}

		return FEonformGridDomain::Make(Dimensions, WorldMin, WorldMax);
	}

	/** Resolve the local domain that graph nodes should publish for this evaluation request. */
	FEonformGridDomain ResolveTargetDomain(
		const FIntPoint& NativeResolution,
		const FVector2d& FallbackWorldMinCm,
		const FVector2d& FallbackWorldMaxCm,
		int32 FallbackBorderSamples = 0) const
	{
		const FEonformGridDomain ReferenceDomain = ResolveReferenceDomain(
			NativeResolution,
			FallbackWorldMinCm,
			FallbackWorldMaxCm);

		FIntPoint Dimensions = NativeResolution;
		if (TargetResolution.X > 1 && TargetResolution.Y > 1)
		{
			Dimensions = TargetResolution;
		}

		if (HasRegion())
		{
			return FEonformGridDomain::Make(
				Dimensions,
				Region.WorldMinCm,
				Region.WorldMaxCm,
				Region.BorderSamples);
		}

		return FEonformGridDomain::Make(
			Dimensions,
			ReferenceDomain.WorldMin,
			ReferenceDomain.WorldMax,
			FMath::Max(FallbackBorderSamples, 0));
	}
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
