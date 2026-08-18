#pragma once

#include "CoreMinimal.h"

struct FTerrainHeightField
{
	int32 Resolution = 0;
	float WorldSize = 0.0f;
	TArray<float> Data;

	void Initialize(int32 InResolution, float InWorldSize)
	{
		Resolution = FMath::Max(2, InResolution);
		WorldSize = FMath::Max(1.0f, InWorldSize);
		Data.SetNumZeroed(Resolution * Resolution);
	}

	FORCEINLINE int32 Index(int32 X, int32 Y) const
	{
		return Y * Resolution + X;
	}

	FORCEINLINE float& At(int32 X, int32 Y)
	{
		return Data[Index(X, Y)];
	}

	FORCEINLINE const float& At(int32 X, int32 Y) const
	{
		return Data[Index(X, Y)];
	}

	bool IsValid() const
	{
		return Resolution >= 2 && Data.Num() == Resolution * Resolution;
	}
};
