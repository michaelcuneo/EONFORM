#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"

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

	FGaeaGridDomain GetGaeaDomain() const
	{
		if (!IsValid())
		{
			return FGaeaGridDomain();
		}

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		return FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));
	}

	FGaeaScalarField ToGaeaScalarField(FName FieldName = TEXT("Height")) const
	{
		FGaeaScalarField Field;
		if (!IsValid())
		{
			return Field;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = FieldName;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		Field.Initialize(GetGaeaDomain(), Descriptor);
		Field.Values = Data;
		return Field;
	}
};
