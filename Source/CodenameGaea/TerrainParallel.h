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

		auto ExecuteChunk = [Resolution, SafeChunkRows, &Function](int32 ChunkIndex)
		{
			const int32 StartY = ChunkIndex * SafeChunkRows;
			const int32 EndY = FMath::Min(StartY + SafeChunkRows, Resolution);
			Function(StartY, EndY);
		};

		// Terrain generation is currently entered from actor construction/editor
		// callbacks on the game thread. Do not dispatch long-running nested work into
		// the Task Graph from that path. Apart from avoiding editor/live-coding task
		// allocator failures, this keeps the helper ready for the intended architecture:
		// once the top-level pure-data build is moved to a background worker, calls made
		// from that worker can use ParallelFor safely.
		if (IsInGameThread())
		{
			for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
			{
				ExecuteChunk(ChunkIndex);
			}
			return;
		}

		ParallelFor(
			DebugName,
			NumChunks,
			1,
			ExecuteChunk,
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

		auto ExecuteChunk = [NumItems, SafeChunkSize, &Function](int32 ChunkIndex)
		{
			const int32 Start = ChunkIndex * SafeChunkSize;
			const int32 End = FMath::Min(Start + SafeChunkSize, NumItems);
			Function(Start, End);
		};

		if (IsInGameThread())
		{
			for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
			{
				ExecuteChunk(ChunkIndex);
			}
			return;
		}

		ParallelFor(
			DebugName,
			NumChunks,
			1,
			ExecuteChunk,
			EParallelForFlags::None);
	}
}
