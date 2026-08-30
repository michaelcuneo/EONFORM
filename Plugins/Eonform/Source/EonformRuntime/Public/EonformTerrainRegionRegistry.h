#pragma once

#include "CoreMinimal.h"

enum class EEonformTerrainRegionEvaluationState : uint8
{
	Known,
	Queued,
	Evaluating,
	Evaluated,
	Failed
};

enum class EEonformTerrainRegionMaterializationState : uint8
{
	Unloaded,
	Prepared,
	Committing,
	Loaded,
	Evicting
};

/**
 * Exact identity of a regional evaluation on the final reference lattice.
 * GenerationRevision is the current generation invalidation token; recipe and
 * cache-context hashes carry the semantic inputs used by the evaluator.
 */
struct EONFORMRUNTIME_API FEonformTerrainRegionKey
{
	uint32 RecipeHash = 0;
	uint64 GenerationRevision = 0;
	uint64 CacheContextRevision = 0;
	FIntPoint ReferenceResolution = FIntPoint::ZeroValue;
	FIntPoint GridDimensions = FIntPoint::ZeroValue;
	FIntPoint StartSample = FIntPoint::ZeroValue;
	FIntPoint EndSample = FIntPoint::ZeroValue;

	bool IsValid() const
	{
		return GenerationRevision != 0
			&& ReferenceResolution.X > 1
			&& ReferenceResolution.Y > 1
			&& GridDimensions.X > 0
			&& GridDimensions.Y > 0
			&& StartSample.X >= 0
			&& StartSample.Y >= 0
			&& EndSample.X > StartSample.X
			&& EndSample.Y > StartSample.Y
			&& EndSample.X < ReferenceResolution.X
			&& EndSample.Y < ReferenceResolution.Y;
	}

	FIntPoint GetRegionResolution() const
	{
		return FIntPoint(
			EndSample.X - StartSample.X + 1,
			EndSample.Y - StartSample.Y + 1);
	}

	bool operator==(const FEonformTerrainRegionKey& Other) const
	{
		return RecipeHash == Other.RecipeHash
			&& GenerationRevision == Other.GenerationRevision
			&& CacheContextRevision == Other.CacheContextRevision
			&& ReferenceResolution == Other.ReferenceResolution
			&& GridDimensions == Other.GridDimensions
			&& StartSample == Other.StartSample
			&& EndSample == Other.EndSample;
	}
};

FORCEINLINE uint32 GetTypeHash(const FEonformTerrainRegionKey& Key)
{
	uint32 Hash = GetTypeHash(Key.RecipeHash);
	Hash = HashCombine(Hash, GetTypeHash(Key.GenerationRevision));
	Hash = HashCombine(Hash, GetTypeHash(Key.CacheContextRevision));
	Hash = HashCombine(Hash, GetTypeHash(Key.ReferenceResolution));
	Hash = HashCombine(Hash, GetTypeHash(Key.GridDimensions));
	Hash = HashCombine(Hash, GetTypeHash(Key.StartSample));
	Hash = HashCombine(Hash, GetTypeHash(Key.EndSample));
	return Hash;
}

/** Identity shared by all regions in the latest final-output generation pass. */
struct EONFORMRUNTIME_API FEonformTerrainRegionPlanIdentity
{
	uint32 RecipeHash = 0;
	uint64 GenerationRevision = 0;
	uint64 CacheContextRevision = 0;
	FIntPoint ReferenceResolution = FIntPoint::ZeroValue;
	FIntPoint GridDimensions = FIntPoint::ZeroValue;

	bool IsValid() const
	{
		return GenerationRevision != 0
			&& ReferenceResolution.X > 1
			&& ReferenceResolution.Y > 1
			&& GridDimensions.X > 0
			&& GridDimensions.Y > 0;
	}

	bool Matches(const FEonformTerrainRegionKey& Key) const
	{
		return IsValid()
			&& Key.RecipeHash == RecipeHash
			&& Key.GenerationRevision == GenerationRevision
			&& Key.CacheContextRevision == CacheContextRevision
			&& Key.ReferenceResolution == ReferenceResolution
			&& Key.GridDimensions == GridDimensions;
	}
};

struct EONFORMRUNTIME_API FEonformTerrainRegionProgress
{
	int64 CompletedWork = 0;
	int64 TotalWork = 0;

	bool IsMeasured() const
	{
		return TotalWork > 0 && CompletedWork >= 0 && CompletedWork <= TotalWork;
	}

	double GetFraction() const
	{
		return IsMeasured()
			? static_cast<double>(CompletedWork) / static_cast<double>(TotalWork)
			: 0.0;
	}
};

struct EONFORMRUNTIME_API FEonformTerrainRegionSnapshot
{
	FEonformTerrainRegionKey Key;
	FIntPoint RegionIndex = FIntPoint::ZeroValue;
	FIntPoint GridDimensions = FIntPoint::ZeroValue;
	EEonformTerrainRegionEvaluationState EvaluationState = EEonformTerrainRegionEvaluationState::Known;
	EEonformTerrainRegionMaterializationState MaterializationState = EEonformTerrainRegionMaterializationState::Unloaded;
	FName Operation = NAME_None;
	FEonformTerrainRegionProgress Progress;
	FString Error;
	int32 VertexCount = 0;
	int32 TriangleCount = 0;
	double LastTransitionSeconds = 0.0;

	bool IsValid() const
	{
		return Key.IsValid()
			&& GridDimensions == Key.GridDimensions
			&& RegionIndex.X >= 0
			&& RegionIndex.Y >= 0
			&& RegionIndex.X < GridDimensions.X
			&& RegionIndex.Y < GridDimensions.Y;
	}

	bool IsLoaded() const
	{
		return MaterializationState == EEonformTerrainRegionMaterializationState::Loaded;
	}

	FIntPoint GetRegionResolution() const
	{
		return Key.GetRegionResolution();
	}
};

/**
 * Runtime-safe authoritative status registry for final-output regional work.
 *
 * This records state only; it does not own terrain fields or Mesh Terrain
 * objects. Evaluation and materialisation are deliberately independent so an
 * already loaded region can remain usable while an identical region key is
 * rebuilt. Old generation records are retained, while UI queries expose only
 * the latest generation-plan identity.
 */
class EONFORMRUNTIME_API FEonformTerrainRegionRegistry
{
public:
	/** Resolve and activate the current generation plan if it is region-equivalent. */
	static bool BeginCurrentPlan(
		const FIntPoint& ReferenceResolution,
		const FIntPoint& GridDimensions,
		FString* OutReason = nullptr);

	static bool RegisterCurrentRegion(
		const FIntPoint& RegionIndex,
		const FIntPoint& StartSample,
		const FIntPoint& EndSample);

	static bool BeginEvaluation(const FIntPoint& StartSample, const FIntPoint& EndSample);
	static bool CompleteEvaluation(
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		int32 VertexCount,
		int32 TriangleCount);
	static bool FailEvaluation(
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		const FString& Error);

	static bool BeginCommit(const FIntPoint& StartSample, const FIntPoint& EndSample);
	static bool MarkLoaded(const FIntPoint& StartSample, const FIntPoint& EndSample);
	static bool FailMaterialization(
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		const FString& Error);
	static bool BeginEviction(const FIntPoint& StartSample, const FIntPoint& EndSample);
	static bool MarkUnloaded(const FIntPoint& StartSample, const FIntPoint& EndSample);

	/** Only store percentages backed by a real completed/total work denominator. */
	static bool SetMeasuredProgress(
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		int64 CompletedWork,
		int64 TotalWork);
	static bool ClearProgress(const FIntPoint& StartSample, const FIntPoint& EndSample);

	static bool GetLatestPlan(
		FEonformTerrainRegionPlanIdentity& OutIdentity,
		TArray<FEonformTerrainRegionSnapshot>& OutRegions,
		uint64* OutChangeSerial = nullptr);

	static uint64 GetChangeSerial();
	static void Reset();
};
