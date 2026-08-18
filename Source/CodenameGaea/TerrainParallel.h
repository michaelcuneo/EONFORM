#pragma once

#include "CoreMinimal.h"
#include "Async/ParallelFor.h"

namespace TerrainParallel
{
	FORCEINLINE int32 RecommendedChunkRows(int32 Resolution)
	{
		if (Resolution >= 4097)
		{
			return 8;
		}
		if (Resolution >= 2049)
		{
			return 16;
		}
		if (Resolution >= 1025)
		{
			return 24;
		}
		return 32;
	}

	template <typename FunctionType>
	FORCEINLINE void ForRows(
		const TCHAR* DebugName,
		int32 Resolution,
		FunctionType&& Function,
		int32 ChunkRows = 0)
	{
		if (Resolution <= 0)
		{
			return;
		}

		const int32 SafeChunkRows = FMath::Max(
			ChunkRows > 0 ? ChunkRows : RecommendedChunkRows(Resolution),
			1);
		const int32 NumChunks = FMath::DivideAndRoundUp(Resolution, SafeChunkRows);

		ParallelFor(
			DebugName,
			NumChunks,
			1,
			[Resolution, SafeChunkRows, &Function](int32 ChunkIndex)
			{
				const int32 StartY = ChunkIndex * SafeChunkRows;
				const int32 EndY = FMath::Min(StartY + SafeChunkRows, Resolution);
				Function(StartY, EndY);
			},
			EParallelForFlags::None);
	}

	template <typename FunctionType>
	FORCEINLINE void ForRange(
		const TCHAR* DebugName,
		int32 NumItems,
		int32 ChunkSize,
		FunctionType&& Function)
	{
		if (NumItems <= 0)
		{
			return;
		}

		const int32 SafeChunkSize = FMath::Max(ChunkSize, 1);
		const int32 NumChunks = FMath::DivideAndRoundUp(NumItems, SafeChunkSize);

		ParallelFor(
			DebugName,
			NumChunks,
			1,
			[NumItems, SafeChunkSize, &Function](int32 ChunkIndex)
			{
				const int32 Start = ChunkIndex * SafeChunkSize;
				const int32 End = FMath::Min(Start + SafeChunkSize, NumItems);
				Function(Start, End);
			},
			EParallelForFlags::None);
	}
}
