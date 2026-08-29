#include "EonformTerrainEvaluator.h"

#include "Misc/ScopeLock.h"

bool FEonformTerrainGlobalSummaryCache::Find(uint64 Key, float& OutValue) const
{
	FScopeLock Lock(&Mutex);
	if (const float* Found = Scalars.Find(Key))
	{
		OutValue = *Found;
		return true;
	}
	return false;
}

void FEonformTerrainGlobalSummaryCache::Store(uint64 Key, float Value)
{
	FScopeLock Lock(&Mutex);
	Scalars.Add(Key, Value);
}

void FEonformTerrainGlobalSummaryCache::Reset()
{
	FScopeLock Lock(&Mutex);
	Scalars.Reset();
}
