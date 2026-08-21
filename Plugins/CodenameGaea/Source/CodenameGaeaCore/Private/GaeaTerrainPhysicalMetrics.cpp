#include "GaeaTerrainPhysicalMetrics.h"

namespace
{
	FCriticalSection PhysicalContextMutex;
	FGaeaTerrainPhysicalMetrics ActivePhysicalMetrics;
	uint64 PhysicalContextRevision = 1;
}

void FGaeaTerrainPhysicalContext::SetActive(const FGaeaTerrainPhysicalMetrics& Metrics)
{
	FScopeLock Lock(&PhysicalContextMutex);
	ActivePhysicalMetrics = Metrics;
	++PhysicalContextRevision;
}

FGaeaTerrainPhysicalMetrics FGaeaTerrainPhysicalContext::GetActive()
{
	FScopeLock Lock(&PhysicalContextMutex);
	return ActivePhysicalMetrics;
}

uint64 FGaeaTerrainPhysicalContext::GetRevision()
{
	FScopeLock Lock(&PhysicalContextMutex);
	return PhysicalContextRevision;
}
