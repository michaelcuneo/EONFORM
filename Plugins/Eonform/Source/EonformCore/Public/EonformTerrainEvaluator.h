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

/**
 * Shared scalar reductions for exact regional evaluation. The cache is carried
 * by FEonformTerrainEvaluationContext so all region requests from one published
 * generation plan see the same world summaries.
 */
class EONFORMCORE_API FEonformTerrainGlobalSummaryCache
{
public:
	bool Find(uint64 Key, float& OutValue) const;
	void Store(uint64 Key, float Value);
	void Reset();

private:
	mutable FCriticalSection Mutex;
	TMap<uint64, float> Scalars;
};

struct EONFORMCORE_API FEonformTerrainEvaluationContext
{
	FEonformTerrainDataset SourceDataset;
	float HeightScale = 1000.0f;

	/** Optional physical world contract supplied by EONFORM's graph asset/output settings. */
	FEonformTerrainPhysicalMetrics PhysicalMetrics = FEonformTerrainPhysicalContext::GetActive();

	/** Preferred interior evaluation resolution. */
	FIntPoint TargetResolution = FIntPoint::ZeroValue;

	/** Virtual full-world resolution used to preserve global procedural scale. */
	FIntPoint ReferenceResolution = FIntPoint::ZeroValue;

	/** Optional local world-space evaluation window. Invalid means evaluate the full world. */
	FEonformTerrainEvaluationRegion Region;

	/**
	 * Preview evaluation may use preview-local global reductions. Final generation
	 * always clears this flag and requires exact full-reference summaries.
	 */
	bool bPreviewEvaluation = false;

	/** Shared exact world reductions used by regional/global-summary nodes. */
	TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe> GlobalSummaryCache =
		MakeShared<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>();

	/** External context revision mixed into incremental signatures. */
	uint64 CacheContextRevision = 0;

	/**
	 * Transient graph pointer installed by FEonformTerrainEvaluator while a node
	 * executes. Global-summary nodes use this to resolve their connected upstream
	 * output without owning or duplicating graph traversal logic.
	 */
	const FEonformTerrainRecipe* ActiveRecipe = nullptr;

	bool HasRegion() const
	{
		return Region.IsValid();
	}

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
	int32 EvaluatedNodeCount = 0;
	int32 CachedNodeCount = 0;
	double EvaluationMilliseconds = 0.0;
};

struct EONFORMCORE_API FEonformTerrainNodeEvaluation
{
	TMap<FName, FEonformTerrainValue> Outputs;

	FEonformTerrainValue* FindOutput(FName Name)
	{
		if (FEonformTerrainValue* Exact = Outputs.Find(Name)) return Exact;
		if (Name == TEXT("Terrain")) return Outputs.Find(TEXT("Out"));
		return nullptr;
	}

	const FEonformTerrainValue* FindOutput(FName Name) const
	{
		if (const FEonformTerrainValue* Exact = Outputs.Find(Name)) return Exact;
		if (Name == TEXT("Terrain")) return Outputs.Find(TEXT("Out"));
		return nullptr;
	}
};

struct EONFORMCORE_API FEonformTerrainEvaluationCacheEntry
{
	uint32 Signature = 0;
	FEonformTerrainNodeEvaluation Evaluation;
};

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

class EONFORMCORE_API FEonformTerrainEvaluator
{
public:
	static FEonformTerrainEvaluationResult Evaluate(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context);

	/**
	 * Evaluates one exact graph output, including scalar ports. This is the shared
	 * graph traversal used by global-summary prepasses; it intentionally does not
	 * require the requested port to be a Terrain value.
	 */
	static bool EvaluateOutput(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context,
		const FGuid& NodeId,
		FName OutputName,
		FEonformTerrainValue& OutValue,
		FString* OutError = nullptr);

	static FEonformTerrainEvaluationResult EvaluateIncremental(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainEvaluationCache& Cache);
};
