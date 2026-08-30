#include "EonformTerrainRegionRegistry.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection RegionRegistryMutex;
	TMap<FEonformTerrainRegionId, FEonformTerrainRegionSnapshot> RegionRegistry;

	FEonformTerrainRegionSnapshot* FindRegion(const FEonformTerrainRegionId& Id)
	{
		return RegionRegistry.Find(Id);
	}
}

bool FEonformTerrainRegionRegistry::RequestRegion(
	const FEonformTerrainRegionId& Id,
	const FIntPoint& GridDimensions,
	const FIntRect& SampleBounds,
	const FIntPoint& TargetResolution,
	uint64 RequestedRevision)
{
	if (!Id.IsValid()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| Id.Coordinate.X < 0
		|| Id.Coordinate.Y < 0
		|| Id.Coordinate.X >= GridDimensions.X
		|| Id.Coordinate.Y >= GridDimensions.Y
		|| TargetResolution.X <= 1
		|| TargetResolution.Y <= 1
		|| RequestedRevision == 0)
	{
		return false;
	}

	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot& Snapshot = RegionRegistry.FindOrAdd(Id);
	Snapshot.Id = Id;
	Snapshot.GridDimensions = GridDimensions;
	Snapshot.SampleBounds = SampleBounds;
	Snapshot.TargetResolution = TargetResolution;
	Snapshot.RequestedRevision = RequestedRevision;
	Snapshot.Stage = EEonformTerrainRegionStage::Queued;
	Snapshot.Operation = NAME_None;
	Snapshot.Progress = FEonformTerrainRegionProgress();
	Snapshot.Error.Reset();
	Snapshot.bDirty = Snapshot.bHasResidentResolution
		&& (Snapshot.ResidentRevision != RequestedRevision || Snapshot.ResidentResolution != TargetResolution);
	return true;
}

bool FEonformTerrainRegionRegistry::SetStage(
	const FEonformTerrainRegionId& Id,
	EEonformTerrainRegionStage Stage,
	FName Operation)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Stage = Stage;
	Snapshot->Operation = Operation;
	Snapshot->Error.Reset();
	Snapshot->Progress = FEonformTerrainRegionProgress();
	if (Snapshot->Residency == EEonformTerrainRegionResidency::Unloaded
		&& (Stage == EEonformTerrainRegionStage::Generating
			|| Stage == EEonformTerrainRegionStage::Meshing
			|| Stage == EEonformTerrainRegionStage::Committing))
	{
		Snapshot->Residency = EEonformTerrainRegionResidency::Loading;
	}
	return true;
}

bool FEonformTerrainRegionRegistry::SetMeasuredProgress(
	const FEonformTerrainRegionId& Id,
	int64 CompletedWork,
	int64 TotalWork)
{
	if (TotalWork <= 0 || CompletedWork < 0 || CompletedWork > TotalWork)
	{
		return false;
	}

	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Progress.CompletedWork = CompletedWork;
	Snapshot->Progress.TotalWork = TotalWork;
	return true;
}

bool FEonformTerrainRegionRegistry::ClearProgress(const FEonformTerrainRegionId& Id)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Progress = FEonformTerrainRegionProgress();
	return true;
}

bool FEonformTerrainRegionRegistry::CommitRegion(
	const FEonformTerrainRegionId& Id,
	uint64 ResidentRevision,
	const FIntPoint& ResidentResolution,
	int32 VertexCount,
	int32 TriangleCount)
{
	if (ResidentRevision == 0
		|| ResidentResolution.X <= 1
		|| ResidentResolution.Y <= 1
		|| VertexCount < 0
		|| TriangleCount < 0)
	{
		return false;
	}

	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Residency = EEonformTerrainRegionResidency::Loaded;
	Snapshot->Stage = EEonformTerrainRegionStage::Resident;
	Snapshot->bHasResidentResolution = true;
	Snapshot->ResidentResolution = ResidentResolution;
	Snapshot->ResidentRevision = ResidentRevision;
	Snapshot->bDirty = Snapshot->ResidentRevision != Snapshot->RequestedRevision
		|| Snapshot->ResidentResolution != Snapshot->TargetResolution;
	Snapshot->Operation = NAME_None;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Snapshot->Error.Reset();
	Snapshot->VertexCount = VertexCount;
	Snapshot->TriangleCount = TriangleCount;
	return true;
}

bool FEonformTerrainRegionRegistry::FailRegion(const FEonformTerrainRegionId& Id, const FString& Error)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Stage = EEonformTerrainRegionStage::Failed;
	Snapshot->Operation = NAME_None;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Snapshot->Error = Error;
	Snapshot->bDirty = Snapshot->bHasResidentResolution;
	if (!Snapshot->bHasResidentResolution)
	{
		Snapshot->Residency = EEonformTerrainRegionResidency::Unloaded;
	}
	return true;
}

bool FEonformTerrainRegionRegistry::BeginEviction(const FEonformTerrainRegionId& Id)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Residency = EEonformTerrainRegionResidency::Evicting;
	Snapshot->Stage = EEonformTerrainRegionStage::Idle;
	Snapshot->Operation = NAME_None;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	return true;
}

bool FEonformTerrainRegionRegistry::MarkUnloaded(const FEonformTerrainRegionId& Id)
{
	FScopeLock Lock(&RegionRegistryMutex);
	FEonformTerrainRegionSnapshot* Snapshot = FindRegion(Id);
	if (!Snapshot)
	{
		return false;
	}

	Snapshot->Residency = EEonformTerrainRegionResidency::Unloaded;
	Snapshot->Stage = EEonformTerrainRegionStage::Idle;
	Snapshot->bHasResidentResolution = false;
	Snapshot->ResidentResolution = FIntPoint::ZeroValue;
	Snapshot->ResidentRevision = 0;
	Snapshot->Operation = NAME_None;
	Snapshot->Progress = FEonformTerrainRegionProgress();
	Snapshot->VertexCount = 0;
	Snapshot->TriangleCount = 0;
	Snapshot->bDirty = Snapshot->RequestedRevision > 0;
	return true;
}

bool FEonformTerrainRegionRegistry::Get(
	const FEonformTerrainRegionId& Id,
	FEonformTerrainRegionSnapshot& OutSnapshot)
{
	FScopeLock Lock(&RegionRegistryMutex);
	const FEonformTerrainRegionSnapshot* Snapshot = RegionRegistry.Find(Id);
	if (!Snapshot)
	{
		return false;
	}

	OutSnapshot = *Snapshot;
	return true;
}

void FEonformTerrainRegionRegistry::GetSourceRegions(
	FName SourceId,
	TArray<FEonformTerrainRegionSnapshot>& OutSnapshots)
{
	OutSnapshots.Reset();
	if (SourceId.IsNone())
	{
		return;
	}

	FScopeLock Lock(&RegionRegistryMutex);
	for (const TPair<FEonformTerrainRegionId, FEonformTerrainRegionSnapshot>& Pair : RegionRegistry)
	{
		if (Pair.Key.SourceId == SourceId)
		{
			OutSnapshots.Add(Pair.Value);
		}
	}

	OutSnapshots.Sort([](const FEonformTerrainRegionSnapshot& A, const FEonformTerrainRegionSnapshot& B)
	{
		return A.Id.Coordinate.Y == B.Id.Coordinate.Y
			? A.Id.Coordinate.X < B.Id.Coordinate.X
			: A.Id.Coordinate.Y < B.Id.Coordinate.Y;
	});
}

bool FEonformTerrainRegionRegistry::RemoveSource(FName SourceId)
{
	if (SourceId.IsNone())
	{
		return false;
	}

	FScopeLock Lock(&RegionRegistryMutex);
	int32 RemovedCount = 0;
	for (auto It = RegionRegistry.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceId == SourceId)
		{
			It.RemoveCurrent();
			++RemovedCount;
		}
	}
	return RemovedCount > 0;
}

void FEonformTerrainRegionRegistry::Reset()
{
	FScopeLock Lock(&RegionRegistryMutex);
	RegionRegistry.Reset();
}
