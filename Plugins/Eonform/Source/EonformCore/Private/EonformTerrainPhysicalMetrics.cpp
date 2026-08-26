#include "EonformTerrainPhysicalMetrics.h"

#include "Misc/ScopeLock.h"

namespace
{
	struct FEonformTerrainPhysicalContextState
	{
		FCriticalSection Mutex;
		FEonformTerrainPhysicalMetrics Metrics = FEonformTerrainPhysicalMetrics(0.0, 0.0, 0.0, 0.0);
		uint64 Revision = 1;
	};

	FEonformTerrainPhysicalContextState& GetPhysicalContextState()
	{
		static FEonformTerrainPhysicalContextState State;
		return State;
	}
}

FEonformTerrainPhysicalMetrics::FEonformTerrainPhysicalMetrics()
{
	const FEonformTerrainPhysicalMetrics Active = FEonformTerrainPhysicalContext::GetActive();
	WorldWidthMeters = Active.WorldWidthMeters;
	WorldDepthMeters = Active.WorldDepthMeters;
	ElevationScaleMeters = Active.ElevationScaleMeters;
	SeaLevelMeters = Active.SeaLevelMeters;
}

void FEonformTerrainPhysicalContext::SetActive(const FEonformTerrainPhysicalMetrics& Metrics)
{
	FEonformTerrainPhysicalContextState& State = GetPhysicalContextState();
	FScopeLock Lock(&State.Mutex);
	State.Metrics = Metrics;
	++State.Revision;
}

FEonformTerrainPhysicalMetrics FEonformTerrainPhysicalContext::GetActive()
{
	FEonformTerrainPhysicalContextState& State = GetPhysicalContextState();
	FScopeLock Lock(&State.Mutex);
	return State.Metrics;
}

uint64 FEonformTerrainPhysicalContext::GetRevision()
{
	FEonformTerrainPhysicalContextState& State = GetPhysicalContextState();
	FScopeLock Lock(&State.Mutex);
	return State.Revision;
}
