#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

struct FTerrainHeightField
{
private:
	FEonformScalarField EonformField;

public:
	int32 Resolution = 0;
	float WorldSize = 0.0f;

	// Legacy compatibility alias. The authoritative storage is EonformField.Values.
	TArray<float>& Data;

	FTerrainHeightField()
		: Data(EonformField.Values)
	{
	}

	FTerrainHeightField(const FTerrainHeightField& Other)
		: EonformField(Other.EonformField)
		, Resolution(Other.Resolution)
		, WorldSize(Other.WorldSize)
		, Data(EonformField.Values)
	{
	}

	FTerrainHeightField(FTerrainHeightField&& Other) noexcept
		: EonformField(MoveTemp(Other.EonformField))
		, Resolution(Other.Resolution)
		, WorldSize(Other.WorldSize)
		, Data(EonformField.Values)
	{
		Other.Resolution = 0;
		Other.WorldSize = 0.0f;
	}

	FTerrainHeightField& operator=(const FTerrainHeightField& Other)
	{
		if (this != &Other)
		{
			EonformField = Other.EonformField;
			Resolution = Other.Resolution;
			WorldSize = Other.WorldSize;
		}
		return *this;
	}

	FTerrainHeightField& operator=(FTerrainHeightField&& Other) noexcept
	{
		if (this != &Other)
		{
			EonformField = MoveTemp(Other.EonformField);
			Resolution = Other.Resolution;
			WorldSize = Other.WorldSize;

			Other.Resolution = 0;
			Other.WorldSize = 0.0f;
		}
		return *this;
	}

	void Initialize(int32 InResolution, float InWorldSize)
	{
		Resolution = FMath::Max(2, InResolution);
		WorldSize = FMath::Max(1.0f, InWorldSize);

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("Height");
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;

		EonformField.Initialize(Domain, Descriptor);
	}

	FORCEINLINE int32 Index(int32 X, int32 Y) const
	{
		return Y * Resolution + X;
	}

	FORCEINLINE float& At(int32 X, int32 Y)
	{
		return EonformField.AtInterior(X, Y);
	}

	FORCEINLINE const float& At(int32 X, int32 Y) const
	{
		return EonformField.AtInterior(X, Y);
	}

	bool IsValid() const
	{
		if (Resolution < 2 || WorldSize < 1.0f || !EonformField.IsValid())
		{
			return false;
		}

		const FEonformGridDomain& Domain = EonformField.Domain;
		if (Domain.Dimensions != FIntPoint(Resolution, Resolution) || Domain.BorderSamples != 0)
		{
			return false;
		}

		const FVector2d Extent = Domain.WorldMax - Domain.WorldMin;
		return Data.Num() == Resolution * Resolution
			&& FMath::IsNearlyEqual(Extent.X, static_cast<double>(WorldSize))
			&& FMath::IsNearlyEqual(Extent.Y, static_cast<double>(WorldSize));
	}

	const FEonformGridDomain& GetEonformDomain() const
	{
		return EonformField.Domain;
	}

	FEonformScalarField& GetEonformField()
	{
		return EonformField;
	}

	const FEonformScalarField& GetEonformField() const
	{
		return EonformField;
	}

	FEonformScalarField ToEonformScalarField(FName FieldName = TEXT("Height")) const
	{
		FEonformScalarField Field = EonformField;
		if (!Field.IsValid())
		{
			return FEonformScalarField();
		}

		Field.Descriptor.Name = FieldName;
		return Field;
	}
};
