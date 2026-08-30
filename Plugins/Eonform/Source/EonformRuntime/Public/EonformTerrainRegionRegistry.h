#pragma once

#include "CoreMinimal.h"

enum class EEonformTerrainRegionResidency : uint8
{
	Unloaded,
	Loading,
	Loaded,
	Evicting
};

enum class EEonformTerrainRegionStage : uint8
{
	Idle,
	Queued,
	Generating,
	Meshing,
	Committing,
	Resident,
	Failed
};

enum class EEonformTerrainRegionQuality : uint8
{
	Q0,
	Q1,
	Q2,
	Q3
};

struct EONFORMRUNTIME_API FEonformTerrainRegionId
{
	FName SourceId = NAME_None;
	FIntPoint Coordinate = FIntPoint::ZeroValue;

	bool IsValid() const
	{
		return !SourceId.IsNone();
	}

	bool operator==(const FEonformTerrainRegionId& Other) const
	{
		return SourceId == Other.SourceId && Coordinate == Other.Coordinate;
	}
};

FORCEINLINE uint32 GetTypeHash(const FEonformTerrainRegionId& Id)
{
	return HashCombine(GetTypeHash(Id.SourceId), GetTypeHash(Id.Coordinate));
}

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
	FEonformTerrainRegionId Id;
	FIntPoint GridDimensions = FIntPoint::ZeroValue;
	FIntRect SampleBounds = FIntRect(0, 0, 0, 0);
	EEonformTerrainRegionResidency Residency = EEonformTerrainRegionResidency::Unloaded;
	EEonformTerrainRegionStage Stage = EEonformTerrainRegionStage::Idle;

	bool bHasResidentQuality = false;
	EEonformTerrainRegionQuality ResidentQuality = EEonformTerrainRegionQuality::Q0;
	EEonformTerrainRegionQuality TargetQuality = EEonformTerrainRegionQuality::Q3;
	uint64 RequestedRevision = 0;
	uint64 ResidentRevision = 0;

	FName Operation = NAME_None;
	FEonformTerrainRegionProgress Progress;
	bool bDirty = false;
	FString Error;
	int32 VertexCount = 0;
	int32 TriangleCount = 0;

	bool IsValid() const
	{
		return Id.IsValid()
			&& GridDimensions.X > 0
			&& GridDimensions.Y > 0
			&& Id.Coordinate.X >= 0
			&& Id.Coordinate.Y >= 0
			&& Id.Coordinate.X < GridDimensions.X
			&& Id.Coordinate.Y < GridDimensions.Y;
	}

	bool HasMeasuredProgress() const
	{
		return Progress.IsMeasured();
	}

	bool IsResidentCurrent() const
	{
		return Residency == EEonformTerrainRegionResidency::Loaded
			&& bHasResidentQuality
			&& ResidentRevision == RequestedRevision
			&& ResidentQuality == TargetQuality
			&& !bDirty;
	}
};

/**
 * Runtime-safe authoritative status registry for spatial terrain regions.
 *
 * The resident state is deliberately independent from the requested state so
 * a usable lower-quality region can remain loaded while a newer or higher-
 * quality revision is being prepared. Progress is only measurable when a
 * producer supplies real completed/total work counts.
 */
class EONFORMRUNTIME_API FEonformTerrainRegionRegistry
{
public:
	static bool RequestRegion(
		const FEonformTerrainRegionId& Id,
		const FIntPoint& GridDimensions,
		const FIntRect& SampleBounds,
		uint64 RequestedRevision,
		EEonformTerrainRegionQuality TargetQuality);

	static bool SetStage(
		const FEonformTerrainRegionId& Id,
		EEonformTerrainRegionStage Stage,
		FName Operation = NAME_None);

	static bool SetMeasuredProgress(
		const FEonformTerrainRegionId& Id,
		int64 CompletedWork,
		int64 TotalWork);

	static bool ClearProgress(const FEonformTerrainRegionId& Id);

	static bool CommitRegion(
		const FEonformTerrainRegionId& Id,
		uint64 ResidentRevision,
		EEonformTerrainRegionQuality ResidentQuality,
		int32 VertexCount = 0,
		int32 TriangleCount = 0);

	static bool FailRegion(const FEonformTerrainRegionId& Id, const FString& Error);
	static bool BeginEviction(const FEonformTerrainRegionId& Id);
	static bool MarkUnloaded(const FEonformTerrainRegionId& Id);

	static bool Get(const FEonformTerrainRegionId& Id, FEonformTerrainRegionSnapshot& OutSnapshot);
	static void GetSourceRegions(FName SourceId, TArray<FEonformTerrainRegionSnapshot>& OutSnapshots);
	static bool RemoveSource(FName SourceId);
	static void Reset();
};
