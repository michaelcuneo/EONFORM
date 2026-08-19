#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"

struct FTerrainHeightField
{
private:
	FGaeaScalarField GaeaField;

public:
	int32 Resolution = 0;
	float WorldSize = 0.0f;

	// Legacy compatibility alias. The authoritative storage is GaeaField.Values.
	TArray<float>& Data;

	FTerrainHeightField()
		: Data(GaeaField.Values)
	{
	}

	FTerrainHeightField(const FTerrainHeightField& Other)
		: GaeaField(Other.GaeaField)
		, Resolution(Other.Resolution)
		, WorldSize(Other.WorldSize)
		, Data(GaeaField.Values)
	{
	}

	FTerrainHeightField(FTerrainHeightField&& Other) noexcept
		: GaeaField(MoveTemp(Other.GaeaField))
		, Resolution(Other.Resolution)
		, WorldSize(Other.WorldSize)
		, Data(GaeaField.Values)
	{
		Other.Resolution = 0;
		Other.WorldSize = 0.0f;
	}

	FTerrainHeightField& operator=(const FTerrainHeightField& Other)
	{
		if (this != &Other)
		{
			GaeaField = Other.GaeaField;
			Resolution = Other.Resolution;
			WorldSize = Other.WorldSize;
		}
		return *this;
	}

	FTerrainHeightField& operator=(FTerrainHeightField&& Other) noexcept
	{
		if (this != &Other)
		{
			GaeaField = MoveTemp(Other.GaeaField);
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
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("Height");
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		GaeaField.Initialize(Domain, Descriptor);
	}

	FORCEINLINE int32 Index(int32 X, int32 Y) const
	{
		return Y * Resolution + X;
	}

	FORCEINLINE float& At(int32 X, int32 Y)
	{
		return GaeaField.AtInterior(X, Y);
	}

	FORCEINLINE const float& At(int32 X, int32 Y) const
	{
		return GaeaField.AtInterior(X, Y);
	}

	bool IsValid() const
	{
		if (Resolution < 2 || WorldSize < 1.0f || !GaeaField.IsValid())
		{
			return false;
		}

		const FGaeaGridDomain& Domain = GaeaField.Domain;
		if (Domain.Dimensions != FIntPoint(Resolution, Resolution) || Domain.BorderSamples != 0)
		{
			return false;
		}

		const FVector2d Extent = Domain.WorldMax - Domain.WorldMin;
		return Data.Num() == Resolution * Resolution
			&& FMath::IsNearlyEqual(Extent.X, static_cast<double>(WorldSize))
			&& FMath::IsNearlyEqual(Extent.Y, static_cast<double>(WorldSize));
	}

	const FGaeaGridDomain& GetGaeaDomain() const
	{
		return GaeaField.Domain;
	}

	FGaeaScalarField& GetGaeaField()
	{
		return GaeaField;
	}

	const FGaeaScalarField& GetGaeaField() const
	{
		return GaeaField;
	}

	FGaeaScalarField ToGaeaScalarField(FName FieldName = TEXT("Height")) const
	{
		FGaeaScalarField Field = GaeaField;
		if (!Field.IsValid())
		{
			return FGaeaScalarField();
		}

		Field.Descriptor.Name = FieldName;
		return Field;
	}
};
