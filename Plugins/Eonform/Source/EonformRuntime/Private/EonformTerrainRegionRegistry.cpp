#include "EonformTerrainRegionRegistry.h"

#include "EonformTerrainGenerationPlan.h"
#include "EonformTerrainRegionalSupport.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection RegionRegistryMutex;
	TMap<FEonformTerrainRegionKey, FEonformTerrainRegionSnapshot> RegionSnapshots;
	FEonformTerrainRegionPlanIdentity LatestPlan;
	uint64 RegionChangeSerial = 1;

	void Touch(FEonformTerrainRegionSnapshot& Snapshot)
	{
		Snapshot.LastTransitionSeconds = FPlatformTime::Seconds();
		++RegionChangeSerial;
	}

	FEonformTerrainRegionKey MakeCurrentKey(
		const FIntPoint& StartSample,
		const FIntPoint& EndSample)
	{
		FEonformTerrainRegionKey Key;
		Key.RecipeHash = LatestPlan.RecipeHash;
		Key.GenerationRevision = LatestPlan.GenerationRevision;
		Key.CacheContextRevision = LatestPlan.CacheContextRevision;
		Key.ReferenceResolution = LatestPlan.ReferenceResolution;
		Key.GridDimensions = LatestPlan.GridDimensions;
		Key.StartSample = StartSample;
		Key.EndSample = EndSample;
		return Key;
	}

	FEonformTerrainRegionSnapshot* FindCurrent(
		const FIntPoint& StartSample,
		const FIntPoint& EndSample)
	{
		if (!LatestPlan.IsValid()) return nullptr;
		return RegionSnapshots.Find(MakeCurrentKey(StartSample, EndSample));
	}
}

bool FEonformTerrainRegionRegistry::BeginCurrentPlan(
	const FIntPoint& ReferenceResolution,
	const FIntPoint& GridDimensions,
	FString* OutReason)
{
	FEonformTerrainGenerationPlan Plan;
	if (!FEonformTerrainGenerationPlanRegistry::Get(Plan) || !Plan.IsValid())
	{
		if (OutReason) *OutReason = TEXT("No valid EONFORM generation plan is available.");
		return false;
	}

	if (ReferenceResolution.X < 2 || ReferenceResolution.Y < 2
		|| GridDimensions.X < 1 || GridDimensions.Y < 1)
	{
		if (OutReason) *OutReason = TEXT("Regional status requires a valid reference resolution and region grid.");
		return false;
	}

	const FEonformTerrainRegionalSupportReport Support =
		FEonformTerrainRegionalSupport::Analyze(Plan.Recipe, ReferenceResolution);
	if (!Support.bSupported)
	{
		if (OutReason) *OutReason = Support.Describe();
		return false;
	}

	FEonformTerrainRegionPlanIdentity Identity;
	Identity.RecipeHash = Plan.Recipe.GetDeterministicHash();
	Identity.GenerationRevision = Plan.Revision;
	Identity.CacheContextRevision = Plan.Context.CacheContextRevision;
	Identity.ReferenceResolution = ReferenceResolution;
	Identity.GridDimensions = GridDimensions;
	if (!Identity.IsValid())
	{
		if (OutReason) *OutReason = TEXT("The current generation plan did not produce a valid regional identity.");
		return false;
	}

	FScopeLock Lock(&RegionRegistryMutex);
	const bool bChanged = !LatestPlan.IsValid()
		|| LatestPlan.RecipeHash != Identity.RecipeHash
		|| LatestPlan.GenerationRevision != Identity.GenerationRevision
		|| LatestPlan.CacheContextRevision != Identity.CacheContextRevision
		|| LatestPlan.ReferenceResolution != Identity.ReferenceResolution
		|| LatestPlan.GridDimensions != Identity.GridDimensions;
	LatestPlan = Identity;
	if (bChanged) ++RegionChangeSerial;
	if (OutReason) OutReason->Reset();
	return true;
}

bool FEonformTerrainRegionRegistry::RegisterCurrentRegion(
	const FIntPoint& RegionIndex,
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	if (!LatestPlan.IsValid()) return false;

	const FEonformTerrainRegionKey Key = MakeCurrentKey(StartSample, EndSample);
	if (!Key.IsValid()
		|| RegionIndex.X < 0 || RegionIndex.Y < 0
		|| RegionIndex.X >= LatestPlan.GridDimensions.X
		|| RegionIndex.Y >= LatestPlan.GridDimensions.Y)
	{
		return false;
	}

	FEonformTerrainRegionSnapshot* Existing = RegionSnapshots.Find(Key);
	if (Existing)
	{
		if (Existing->RegionIndex != RegionIndex || Existing->GridDimensions != LatestPlan.GridDimensions)
		{
			Existing->RegionIndex = RegionIndex;
			Existing->GridDimensions = LatestPlan.GridDimensions;
			Touch(*Existing);
		}
		return true;
	}

	FEonformTerrainRegionSnapshot Snapshot;
	Snapshot.Key = Key;
	Snapshot.RegionIndex = RegionIndex;
	Snapshot.GridDimensions = LatestPlan.GridDimensions;
	Snapshot.EvaluationState = EEonformTerrainRegionEvaluationState::Known;
	Snapshot.MaterializationState = EEonformTerrainRegionMaterializationState::Unloaded;
	Touch(Snapshot);
	RegionSnapshots.Add(Key, MoveTemp(Snapshot));
	return true;
}

bool FEonformTerrainRegionRegistry::BeginEvaluation(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->EvaluationState = EEonformTerrainRegionEvaluationState::Evaluating;
	Snapshot->Operation = TEXT("Evaluate");
	Snapshot->Error.Reset();
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::CompleteEvaluation(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample,
	int32 VertexCount,
	int32 TriangleCount)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->EvaluationState = EEonformTerrainRegionEvaluationState::Evaluated;
	Snapshot->MaterializationState = EEonformTerrainRegionMaterializationState::Prepared;
	Snapshot->Operation = TEXT("Mesh prepared");
	Snapshot->Error.Reset();
	Snapshot->VertexCount = FMath::Max(VertexCount, 0);
	Snapshot->TriangleCount = FMath::Max(TriangleCount, 0);
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::FailEvaluation(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample,
	const FString& Error)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->EvaluationState = EEonformTerrainRegionEvaluationState::Failed;
	Snapshot->Operation = TEXT("Evaluate");
	Snapshot->Error = Error;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::BeginCommit(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->MaterializationState = EEonformTerrainRegionMaterializationState::Committing;
	Snapshot->Operation = TEXT("Mesh Terrain commit");
	Snapshot->Error.Reset();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::MarkLoaded(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->MaterializationState = EEonformTerrainRegionMaterializationState::Loaded;
	Snapshot->Operation = TEXT("Resident provider mesh");
	Snapshot->Error.Reset();
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::FailMaterialization(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample,
	const FString& Error)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->Operation = TEXT("Mesh Terrain commit");
	Snapshot->Error = Error;
	if (Snapshot->MaterializationState != EEonformTerrainRegionMaterializationState::Loaded)
	{
		Snapshot->MaterializationState = EEonformTerrainRegionMaterializationState::Prepared;
	}
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::BeginEviction(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->MaterializationState = EEonformTerrainRegionMaterializationState::Evicting;
	Snapshot->Operation = TEXT("Evict");
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::MarkUnloaded(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->MaterializationState = EEonformTerrainRegionMaterializationState::Unloaded;
	Snapshot->Operation = NAME_None;
	Snapshot->VertexCount = 0;
	Snapshot->TriangleCount = 0;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::SetMeasuredProgress(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample,
	int64 CompletedWork,
	int64 TotalWork)
{
	if (TotalWork <= 0 || CompletedWork < 0 || CompletedWork > TotalWork) return false;
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->Progress.CompletedWork = CompletedWork;
	Snapshot->Progress.TotalWork = TotalWork;
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::ClearProgress(
	const FIntPoint& StartSample,
	const FIntPoint& EndSample)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindCurrent(StartSample, EndSample);
	if (!Snapshot) return false;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Touch(*Snapshot);
	return true;
}

bool FEonformTerrainRegionRegistry::GetLatestPlan(
	FEonformTerrainRegionPlanIdentity& OutIdentity,
	TArray<FEonformTerrainRegionSnapshot>& OutRegions,
	uint64* OutChangeSerial)
{
	FScopeLock Lock(&RegionRegistryMutex);
	OutIdentity = LatestPlan;
	OutRegions.Reset();
	if (OutChangeSerial) *OutChangeSerial = RegionChangeSerial;
	if (!LatestPlan.IsValid()) return false;

	for (const TPair<FEonformTerrainRegionKey, FEonformTerrainRegionSnapshot>& Pair : RegionSnapshots)
	{
		if (LatestPlan.Matches(Pair.Key)) OutRegions.Add(Pair.Value);
	}
	OutRegions.Sort([](const FEonformTerrainRegionSnapshot& A, const FEonformTerrainRegionSnapshot& B)
	{
		if (A.RegionIndex.Y != B.RegionIndex.Y) return A.RegionIndex.Y < B.RegionIndex.Y;
		return A.RegionIndex.X < B.RegionIndex.X;
	});
	return true;
}

uint64 FEonformTerrainRegionRegistry::GetChangeSerial()
{
	FScopeLock Lock(&RegionRegistryMutex);
	return RegionChangeSerial;
}

void FEonformTerrainRegionRegistry::Reset()
{
	FScopeLock Lock(&RegionRegistryMutex);
	RegionSnapshots.Reset();
	LatestPlan = FEonformTerrainRegionPlanIdentity();
	++RegionChangeSerial;
}
