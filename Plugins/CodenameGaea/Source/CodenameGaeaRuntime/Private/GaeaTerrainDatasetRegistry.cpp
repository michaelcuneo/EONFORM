#include "GaeaTerrainDatasetRegistry.h"

#include "GaeaTerrainFieldNames.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection RegistryMutex;
	TMap<FName, FGaeaTerrainDatasetSnapshot> Registry;
	uint64 NextRevision = 1;
	FName LatestSourceId = NAME_None;

	uint64 PublishInternal(
		FName SourceId,
		FGaeaTerrainDataset&& Dataset,
		const FGaeaTerrainDatasetMetadata& Metadata)
	{
		if (SourceId.IsNone() || Dataset.IsEmpty() || Metadata.HeightScale <= UE_SMALL_NUMBER)
		{
			return 0;
		}

		FScopeLock Lock(&RegistryMutex);

		FGaeaTerrainDatasetSnapshot Snapshot;
		Snapshot.SourceId = SourceId;
		Snapshot.Revision = NextRevision++;
		Snapshot.Metadata = Metadata;
		Snapshot.Dataset = MoveTemp(Dataset);

		const uint64 Revision = Snapshot.Revision;
		Registry.Add(SourceId, MoveTemp(Snapshot));
		LatestSourceId = SourceId;
		return Revision;
	}
}

uint64 FGaeaTerrainDatasetRegistry::Publish(
	FName SourceId,
	const FGaeaTerrainDataset& Dataset,
	const FGaeaTerrainDatasetMetadata& Metadata)
{
	FGaeaTerrainDataset Copy = Dataset;
	return PublishInternal(SourceId, MoveTemp(Copy), Metadata);
}

uint64 FGaeaTerrainDatasetRegistry::Publish(
	FName SourceId,
	FGaeaTerrainDataset&& Dataset,
	const FGaeaTerrainDatasetMetadata& Metadata)
{
	return PublishInternal(SourceId, MoveTemp(Dataset), Metadata);
}

bool FGaeaTerrainDatasetRegistry::Get(FName SourceId, FGaeaTerrainDatasetSnapshot& OutSnapshot)
{
	FScopeLock Lock(&RegistryMutex);
	const FGaeaTerrainDatasetSnapshot* Snapshot = Registry.Find(SourceId);
	if (!Snapshot)
	{
		return false;
	}

	OutSnapshot = *Snapshot;
	return true;
}

bool FGaeaTerrainDatasetRegistry::GetLatest(FGaeaTerrainDatasetSnapshot& OutSnapshot)
{
	FScopeLock Lock(&RegistryMutex);
	if (LatestSourceId.IsNone())
	{
		return false;
	}

	const FGaeaTerrainDatasetSnapshot* Snapshot = Registry.Find(LatestSourceId);
	if (!Snapshot)
	{
		return false;
	}

	OutSnapshot = *Snapshot;
	return true;
}

bool FGaeaTerrainDatasetRegistry::GetHeightResolution(FName SourceId, FIntPoint& OutResolution, uint64* OutRevision)
{
	OutResolution = FIntPoint::ZeroValue;
	if (OutRevision)
	{
		*OutRevision = 0;
	}

	FScopeLock Lock(&RegistryMutex);
	const FGaeaTerrainDatasetSnapshot* Snapshot = Registry.Find(SourceId);
	if (!Snapshot || !Snapshot->IsValid())
	{
		return false;
	}

	const FGaeaScalarField* HeightField = Snapshot->Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (!HeightField)
	{
		HeightField = Snapshot->Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
	}
	if (!HeightField || !HeightField->IsValid())
	{
		return false;
	}

	OutResolution = HeightField->Domain.Dimensions;
	if (OutRevision)
	{
		*OutRevision = Snapshot->Revision;
	}
	return OutResolution.X > 1 && OutResolution.Y > 1;
}

bool FGaeaTerrainDatasetRegistry::Remove(FName SourceId)
{
	FScopeLock Lock(&RegistryMutex);
	const bool bRemoved = Registry.Remove(SourceId) > 0;
	if (bRemoved && LatestSourceId == SourceId)
	{
		LatestSourceId = NAME_None;
		uint64 HighestRevision = 0;
		for (const TPair<FName, FGaeaTerrainDatasetSnapshot>& Pair : Registry)
		{
			if (Pair.Value.Revision > HighestRevision)
			{
				HighestRevision = Pair.Value.Revision;
				LatestSourceId = Pair.Key;
			}
		}
	}
	return bRemoved;
}

void FGaeaTerrainDatasetRegistry::Reset()
{
	FScopeLock Lock(&RegistryMutex);
	Registry.Reset();
	LatestSourceId = NAME_None;
	NextRevision = 1;
}
