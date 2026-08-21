#include "GaeaTerrainDatasetRegistry.h"

#include "GaeaTerrainFieldNames.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection RegistryMutex;
	TMap<FName, FGaeaTerrainDatasetSnapshot> Registry;
	TMap<FName, FGaeaTerrainHeightStatistics> HeightStatistics;
	uint64 NextRevision = 1;
	FName LatestSourceId = NAME_None;

	FGaeaTerrainHeightStatistics BuildHeightStatistics(const FGaeaTerrainDataset& Dataset)
	{
		FGaeaTerrainHeightStatistics Statistics;
		const FGaeaScalarField* HeightField = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!HeightField)
		{
			HeightField = Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
		}
		if (!HeightField || !HeightField->IsValid())
		{
			return Statistics;
		}

		Statistics.Resolution = HeightField->Domain.Dimensions;
		Statistics.Minimum = TNumericLimits<double>::Max();
		Statistics.Maximum = TNumericLimits<double>::Lowest();
		double Sum = 0.0;
		constexpr double SeaTolerance = 1.0e-6;

		for (int32 Y = 0; Y < HeightField->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < HeightField->Domain.Dimensions.X; ++X)
			{
				const double Value = static_cast<double>(HeightField->AtInterior(X, Y));
				Statistics.Minimum = FMath::Min(Statistics.Minimum, Value);
				Statistics.Maximum = FMath::Max(Statistics.Maximum, Value);
				Sum += Value;
				++Statistics.SampleCount;

				if (Value > SeaTolerance)
				{
					++Statistics.LandSampleCount;
				}
				else if (Value < -SeaTolerance)
				{
					++Statistics.UnderwaterSampleCount;
				}
				else
				{
					++Statistics.SeaLevelSampleCount;
				}
			}
		}

		if (Statistics.SampleCount > 0)
		{
			Statistics.Mean = Sum / static_cast<double>(Statistics.SampleCount);
		}
		else
		{
			Statistics.Minimum = 0.0;
			Statistics.Maximum = 0.0;
		}
		return Statistics;
	}

	uint64 PublishInternal(
		FName SourceId,
		FGaeaTerrainDataset&& Dataset,
		const FGaeaTerrainDatasetMetadata& Metadata)
	{
		if (SourceId.IsNone()
			|| Dataset.IsEmpty()
			|| Metadata.HeightScale <= UE_SMALL_NUMBER
			|| !FMath::IsNearlyZero(Metadata.SeaLevelNormalized))
		{
			return 0;
		}

		const FGaeaTerrainHeightStatistics Statistics = BuildHeightStatistics(Dataset);
		FScopeLock Lock(&RegistryMutex);

		FGaeaTerrainDatasetSnapshot Snapshot;
		Snapshot.SourceId = SourceId;
		Snapshot.Revision = NextRevision++;
		Snapshot.Metadata = Metadata;
		Snapshot.Dataset = MoveTemp(Dataset);

		const uint64 Revision = Snapshot.Revision;
		Registry.Add(SourceId, MoveTemp(Snapshot));
		if (Statistics.IsValid())
		{
			HeightStatistics.Add(SourceId, Statistics);
		}
		else
		{
			HeightStatistics.Remove(SourceId);
		}
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

	const FGaeaTerrainHeightStatistics* Statistics = HeightStatistics.Find(SourceId);
	if (!Statistics || !Statistics->IsValid())
	{
		return false;
	}

	OutResolution = Statistics->Resolution;
	if (OutRevision)
	{
		*OutRevision = Snapshot->Revision;
	}
	return true;
}

bool FGaeaTerrainDatasetRegistry::GetHeightStatistics(
	FName SourceId,
	FGaeaTerrainHeightStatistics& OutStatistics,
	uint64* OutRevision)
{
	OutStatistics = FGaeaTerrainHeightStatistics();
	if (OutRevision)
	{
		*OutRevision = 0;
	}

	FScopeLock Lock(&RegistryMutex);
	const FGaeaTerrainDatasetSnapshot* Snapshot = Registry.Find(SourceId);
	const FGaeaTerrainHeightStatistics* Statistics = HeightStatistics.Find(SourceId);
	if (!Snapshot || !Snapshot->IsValid() || !Statistics || !Statistics->IsValid())
	{
		return false;
	}

	OutStatistics = *Statistics;
	if (OutRevision)
	{
		*OutRevision = Snapshot->Revision;
	}
	return true;
}

bool FGaeaTerrainDatasetRegistry::Remove(FName SourceId)
{
	FScopeLock Lock(&RegistryMutex);
	const bool bRemoved = Registry.Remove(SourceId) > 0;
	HeightStatistics.Remove(SourceId);
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
	HeightStatistics.Reset();
	LatestSourceId = NAME_None;
	NextRevision = 1;
}
