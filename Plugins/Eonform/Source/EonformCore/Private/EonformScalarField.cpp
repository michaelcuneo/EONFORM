#include "EonformScalarField.h"

void FEonformScalarField::Initialize(
	const FEonformGridDomain& InDomain,
	const FEonformFieldDescriptor& InDescriptor,
	float InitialValue)
{
	Domain = InDomain;
	Descriptor = InDescriptor;

	const int32 SampleCount = Domain.GetStorageSampleCount();
	Values.SetNum(SampleCount);
	if (SampleCount > 0)
	{
		Values.Init(InitialValue, SampleCount);
	}
}

bool FEonformScalarField::IsValid() const
{
	return Domain.IsValid()
		&& Values.Num() == Domain.GetStorageSampleCount();
}

void FEonformScalarField::Fill(float Value)
{
	if (Values.Num() > 0)
	{
		Values.Init(Value, Values.Num());
	}
}

float& FEonformScalarField::AtStorage(int32 X, int32 Y)
{
	check(IsValid());
	return Values[Domain.GetStorageIndex(X, Y)];
}

const float& FEonformScalarField::AtStorage(int32 X, int32 Y) const
{
	check(IsValid());
	return Values[Domain.GetStorageIndex(X, Y)];
}

float& FEonformScalarField::AtInterior(int32 X, int32 Y)
{
	check(IsValid());
	check(Domain.IsInteriorCoordinate(X, Y));
	return AtStorage(X + Domain.BorderSamples, Y + Domain.BorderSamples);
}

const float& FEonformScalarField::AtInterior(int32 X, int32 Y) const
{
	check(IsValid());
	check(Domain.IsInteriorCoordinate(X, Y));
	return AtStorage(X + Domain.BorderSamples, Y + Domain.BorderSamples);
}

float FEonformScalarField::SampleWorld(const FVector2d& WorldPosition, bool bClampToDomain) const
{
	if (!IsValid())
	{
		return 0.0f;
	}

	const FVector2d EvaluationMin = Domain.GetEvaluationMin();
	const FVector2d EvaluationMax = Domain.GetEvaluationMax();

	FVector2d SamplePosition = WorldPosition;
	if (bClampToDomain)
	{
		SamplePosition.X = FMath::Clamp(SamplePosition.X, EvaluationMin.X, EvaluationMax.X);
		SamplePosition.Y = FMath::Clamp(SamplePosition.Y, EvaluationMin.Y, EvaluationMax.Y);
	}
	else if (!Domain.ContainsWorldPosition(SamplePosition, true))
	{
		return 0.0f;
	}

	const FVector2d GridCoordinate = Domain.WorldToStorageCoordinate(SamplePosition);
	const FIntPoint StorageDimensions = Domain.GetStorageDimensions();

	if (Descriptor.Interpolation == EEonformInterpolation::Nearest)
	{
		const int32 X = FMath::Clamp(FMath::RoundToInt(GridCoordinate.X), 0, StorageDimensions.X - 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(GridCoordinate.Y), 0, StorageDimensions.Y - 1);
		return AtStorage(X, Y);
	}

	const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridCoordinate.X), 0, StorageDimensions.X - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridCoordinate.Y), 0, StorageDimensions.Y - 1);
	const int32 X1 = FMath::Min(X0 + 1, StorageDimensions.X - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, StorageDimensions.Y - 1);

	const double AlphaX = FMath::Clamp(GridCoordinate.X - static_cast<double>(X0), 0.0, 1.0);
	const double AlphaY = FMath::Clamp(GridCoordinate.Y - static_cast<double>(Y0), 0.0, 1.0);

	const double A = FMath::Lerp(
		static_cast<double>(AtStorage(X0, Y0)),
		static_cast<double>(AtStorage(X1, Y0)),
		AlphaX);
	const double B = FMath::Lerp(
		static_cast<double>(AtStorage(X0, Y1)),
		static_cast<double>(AtStorage(X1, Y1)),
		AlphaX);

	return static_cast<float>(FMath::Lerp(A, B, AlphaY));
}
