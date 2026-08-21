#include "GaeaTerrainPhysicalMetrics.h"

namespace
{
	struct FGaeaTerrainPhysicalContextState
	{
		FCriticalSection Mutex;
		FGaeaTerrainPhysicalMetrics Metrics = FGaeaTerrainPhysicalMetrics(0.0, 0.0, 0.0, 0.0);
		uint64 Revision = 1;
	};

	FGaeaTerrainPhysicalContextState& GetPhysicalContextState()
	{
		static FGaeaTerrainPhysicalContextState State;
		return State;
	}
}

FGaeaTerrainPhysicalMetrics::FGaeaTerrainPhysicalMetrics()
{
	const FGaeaTerrainPhysicalMetrics Active = FGaeaTerrainPhysicalContext::GetActive();
	WorldWidthMeters = Active.WorldWidthMeters;
	WorldDepthMeters = Active.WorldDepthMeters;
	ElevationScaleMeters = Active.ElevationScaleMeters;
	SeaLevelMeters = Active.SeaLevelMeters;
}

void FGaeaTerrainPhysicalContext::SetActive(const FGaeaTerrainPhysicalMetrics& Metrics)
{
	FGaeaTerrainPhysicalContextState& State = GetPhysicalContextState();
	FScopeLock Lock(&State.Mutex);
	State.Metrics = Metrics;
	++State.Revision;
}

FGaeaTerrainPhysicalMetrics FGaeaTerrainPhysicalContext::GetActive()
{
	FGaeaTerrainPhysicalContextState& State = GetPhysicalContextState();
	FScopeLock Lock(&State.Mutex);
	return State.Metrics;
}

uint64 FGaeaTerrainPhysicalContext::GetRevision()
{
	FGaeaTerrainPhysicalContextState& State = GetPhysicalContextState();
	FScopeLock Lock(&State.Mutex);
	return State.Revision;
}
