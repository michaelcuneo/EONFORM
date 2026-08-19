#pragma once

#include "CoreMinimal.h"

/**
 * Describes a regular 2D sample grid in world space.
 *
 * Dimensions and WorldMin/WorldMax describe the requested interior grid.
 * BorderSamples adds an evaluation guard band around that grid without
 * changing the interior resolution or spacing.
 */
struct CODENAMEGAEACORE_API FGaeaGridDomain
{
	FIntPoint Dimensions = FIntPoint::ZeroValue;
	FVector2d WorldMin = FVector2d::ZeroVector;
	FVector2d WorldMax = FVector2d::ZeroVector;
	int32 BorderSamples = 0;

	static FGaeaGridDomain Make(
		const FIntPoint& InDimensions,
		const FVector2d& InWorldMin,
		const FVector2d& InWorldMax,
		int32 InBorderSamples = 0);

	bool operator==(const FGaeaGridDomain& Other) const
	{
		return Dimensions == Other.Dimensions
			&& WorldMin == Other.WorldMin
			&& WorldMax == Other.WorldMax
			&& BorderSamples == Other.BorderSamples;
	}

	bool operator!=(const FGaeaGridDomain& Other) const
	{
		return !(*this == Other);
	}

	bool IsValid() const;

	int32 GetInteriorSampleCount() const;
	FIntPoint GetStorageDimensions() const;
	int32 GetStorageSampleCount() const;

	FVector2d GetCellSize() const;
	FVector2d GetEvaluationMin() const;
	FVector2d GetEvaluationMax() const;

	bool IsInteriorCoordinate(int32 X, int32 Y) const;
	bool IsStorageCoordinate(int32 X, int32 Y) const;

	int32 GetInteriorIndex(int32 X, int32 Y) const;
	int32 GetStorageIndex(int32 X, int32 Y) const;

	FVector2d InteriorSampleToWorld(int32 X, int32 Y) const;
	FVector2d StorageSampleToWorld(int32 X, int32 Y) const;
	FVector2d WorldToStorageCoordinate(const FVector2d& WorldPosition) const;

	bool ContainsWorldPosition(const FVector2d& WorldPosition, bool bIncludeBorder = true) const;
};
