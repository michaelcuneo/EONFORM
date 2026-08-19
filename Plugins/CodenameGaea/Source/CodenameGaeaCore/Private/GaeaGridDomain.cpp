#include "GaeaGridDomain.h"

FGaeaGridDomain FGaeaGridDomain::Make(
	const FIntPoint& InDimensions,
	const FVector2d& InWorldMin,
	const FVector2d& InWorldMax,
	int32 InBorderSamples)
{
	FGaeaGridDomain Domain;
	Domain.Dimensions = InDimensions;
	Domain.WorldMin = InWorldMin;
	Domain.WorldMax = InWorldMax;
	Domain.BorderSamples = InBorderSamples;
	return Domain;
}

bool FGaeaGridDomain::IsValid() const
{
	return Dimensions.X >= 2
		&& Dimensions.Y >= 2
		&& BorderSamples >= 0
		&& FMath::IsFinite(WorldMin.X)
		&& FMath::IsFinite(WorldMin.Y)
		&& FMath::IsFinite(WorldMax.X)
		&& FMath::IsFinite(WorldMax.Y)
		&& WorldMax.X > WorldMin.X
		&& WorldMax.Y > WorldMin.Y;
}

int32 FGaeaGridDomain::GetInteriorSampleCount() const
{
	return IsValid() ? Dimensions.X * Dimensions.Y : 0;
}

FIntPoint FGaeaGridDomain::GetStorageDimensions() const
{
	if (!IsValid())
	{
		return FIntPoint::ZeroValue;
	}

	return FIntPoint(
		Dimensions.X + BorderSamples * 2,
		Dimensions.Y + BorderSamples * 2);
}

int32 FGaeaGridDomain::GetStorageSampleCount() const
{
	const FIntPoint StorageDimensions = GetStorageDimensions();
	return StorageDimensions.X * StorageDimensions.Y;
}

FVector2d FGaeaGridDomain::GetCellSize() const
{
	if (!IsValid())
	{
		return FVector2d::ZeroVector;
	}

	return FVector2d(
		(WorldMax.X - WorldMin.X) / static_cast<double>(Dimensions.X - 1),
		(WorldMax.Y - WorldMin.Y) / static_cast<double>(Dimensions.Y - 1));
}

FVector2d FGaeaGridDomain::GetEvaluationMin() const
{
	return IsValid()
		? WorldMin - GetCellSize() * static_cast<double>(BorderSamples)
		: FVector2d::ZeroVector;
}

FVector2d FGaeaGridDomain::GetEvaluationMax() const
{
	return IsValid()
		? WorldMax + GetCellSize() * static_cast<double>(BorderSamples)
		: FVector2d::ZeroVector;
}

bool FGaeaGridDomain::IsInteriorCoordinate(int32 X, int32 Y) const
{
	return IsValid()
		&& X >= 0 && X < Dimensions.X
		&& Y >= 0 && Y < Dimensions.Y;
}

bool FGaeaGridDomain::IsStorageCoordinate(int32 X, int32 Y) const
{
	const FIntPoint StorageDimensions = GetStorageDimensions();
	return X >= 0 && X < StorageDimensions.X
		&& Y >= 0 && Y < StorageDimensions.Y;
}

int32 FGaeaGridDomain::GetInteriorIndex(int32 X, int32 Y) const
{
	check(IsInteriorCoordinate(X, Y));
	return Y * Dimensions.X + X;
}

int32 FGaeaGridDomain::GetStorageIndex(int32 X, int32 Y) const
{
	check(IsStorageCoordinate(X, Y));
	return Y * GetStorageDimensions().X + X;
}

FVector2d FGaeaGridDomain::InteriorSampleToWorld(int32 X, int32 Y) const
{
	check(IsInteriorCoordinate(X, Y));
	const FVector2d CellSize = GetCellSize();
	return WorldMin + FVector2d(
		static_cast<double>(X) * CellSize.X,
		static_cast<double>(Y) * CellSize.Y);
}

FVector2d FGaeaGridDomain::StorageSampleToWorld(int32 X, int32 Y) const
{
	check(IsStorageCoordinate(X, Y));
	const FVector2d CellSize = GetCellSize();
	return GetEvaluationMin() + FVector2d(
		static_cast<double>(X) * CellSize.X,
		static_cast<double>(Y) * CellSize.Y);
}

FVector2d FGaeaGridDomain::WorldToStorageCoordinate(const FVector2d& WorldPosition) const
{
	if (!IsValid())
	{
		return FVector2d::ZeroVector;
	}

	const FVector2d CellSize = GetCellSize();
	const FVector2d EvaluationMin = GetEvaluationMin();
	return FVector2d(
		(WorldPosition.X - EvaluationMin.X) / CellSize.X,
		(WorldPosition.Y - EvaluationMin.Y) / CellSize.Y);
}

bool FGaeaGridDomain::ContainsWorldPosition(const FVector2d& WorldPosition, bool bIncludeBorder) const
{
	if (!IsValid())
	{
		return false;
	}

	const FVector2d Min = bIncludeBorder ? GetEvaluationMin() : WorldMin;
	const FVector2d Max = bIncludeBorder ? GetEvaluationMax() : WorldMax;
	return WorldPosition.X >= Min.X && WorldPosition.X <= Max.X
		&& WorldPosition.Y >= Min.Y && WorldPosition.Y <= Max.Y;
}
