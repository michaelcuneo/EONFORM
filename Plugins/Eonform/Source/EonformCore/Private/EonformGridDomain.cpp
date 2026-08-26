#include "EonformGridDomain.h"

FEonformGridDomain FEonformGridDomain::Make(
	const FIntPoint& InDimensions,
	const FVector2d& InWorldMin,
	const FVector2d& InWorldMax,
	int32 InBorderSamples)
{
	FEonformGridDomain Domain;
	Domain.Dimensions = InDimensions;
	Domain.WorldMin = InWorldMin;
	Domain.WorldMax = InWorldMax;
	Domain.BorderSamples = InBorderSamples;
	return Domain;
}

bool FEonformGridDomain::IsValid() const
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

int32 FEonformGridDomain::GetInteriorSampleCount() const
{
	return IsValid() ? Dimensions.X * Dimensions.Y : 0;
}

FIntPoint FEonformGridDomain::GetStorageDimensions() const
{
	if (!IsValid())
	{
		return FIntPoint::ZeroValue;
	}

	return FIntPoint(
		Dimensions.X + BorderSamples * 2,
		Dimensions.Y + BorderSamples * 2);
}

int32 FEonformGridDomain::GetStorageSampleCount() const
{
	const FIntPoint StorageDimensions = GetStorageDimensions();
	return StorageDimensions.X * StorageDimensions.Y;
}

FVector2d FEonformGridDomain::GetCellSize() const
{
	if (!IsValid())
	{
		return FVector2d::ZeroVector;
	}

	return FVector2d(
		(WorldMax.X - WorldMin.X) / static_cast<double>(Dimensions.X - 1),
		(WorldMax.Y - WorldMin.Y) / static_cast<double>(Dimensions.Y - 1));
}

FVector2d FEonformGridDomain::GetEvaluationMin() const
{
	return IsValid()
		? WorldMin - GetCellSize() * static_cast<double>(BorderSamples)
		: FVector2d::ZeroVector;
}

FVector2d FEonformGridDomain::GetEvaluationMax() const
{
	return IsValid()
		? WorldMax + GetCellSize() * static_cast<double>(BorderSamples)
		: FVector2d::ZeroVector;
}

bool FEonformGridDomain::IsInteriorCoordinate(int32 X, int32 Y) const
{
	return IsValid()
		&& X >= 0 && X < Dimensions.X
		&& Y >= 0 && Y < Dimensions.Y;
}

bool FEonformGridDomain::IsStorageCoordinate(int32 X, int32 Y) const
{
	const FIntPoint StorageDimensions = GetStorageDimensions();
	return X >= 0 && X < StorageDimensions.X
		&& Y >= 0 && Y < StorageDimensions.Y;
}

int32 FEonformGridDomain::GetInteriorIndex(int32 X, int32 Y) const
{
	check(IsInteriorCoordinate(X, Y));
	return Y * Dimensions.X + X;
}

int32 FEonformGridDomain::GetStorageIndex(int32 X, int32 Y) const
{
	check(IsStorageCoordinate(X, Y));
	return Y * GetStorageDimensions().X + X;
}

FVector2d FEonformGridDomain::InteriorSampleToWorld(int32 X, int32 Y) const
{
	check(IsInteriorCoordinate(X, Y));
	const FVector2d CellSize = GetCellSize();
	return WorldMin + FVector2d(
		static_cast<double>(X) * CellSize.X,
		static_cast<double>(Y) * CellSize.Y);
}

FVector2d FEonformGridDomain::StorageSampleToWorld(int32 X, int32 Y) const
{
	check(IsStorageCoordinate(X, Y));
	const FVector2d CellSize = GetCellSize();
	return GetEvaluationMin() + FVector2d(
		static_cast<double>(X) * CellSize.X,
		static_cast<double>(Y) * CellSize.Y);
}

FVector2d FEonformGridDomain::WorldToStorageCoordinate(const FVector2d& WorldPosition) const
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

bool FEonformGridDomain::ContainsWorldPosition(const FVector2d& WorldPosition, bool bIncludeBorder) const
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
