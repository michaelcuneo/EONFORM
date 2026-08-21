#pragma once

#include "CoreMinimal.h"
#include "GaeaGridDomain.h"

enum class EGaeaFieldUnit : uint8
{
	Unitless,
	Normalized,
	Centimeters,
	Meters,
	Kilometers,
	SquareKilometers,
	Degrees,
	Celsius
};

enum class EGaeaInterpolation : uint8
{
	Nearest,
	Bilinear
};

struct CODENAMEGAEACORE_API FGaeaFieldDescriptor
{
	FName Name = NAME_None;
	EGaeaFieldUnit Unit = EGaeaFieldUnit::Unitless;
	EGaeaInterpolation Interpolation = EGaeaInterpolation::Bilinear;
};

/**
 * Dense scalar values sampled over an FGaeaGridDomain.
 * Storage includes the domain guard band when BorderSamples > 0.
 */
struct CODENAMEGAEACORE_API FGaeaScalarField
{
	FGaeaGridDomain Domain;
	FGaeaFieldDescriptor Descriptor;
	TArray<float> Values;

	void Initialize(
		const FGaeaGridDomain& InDomain,
		const FGaeaFieldDescriptor& InDescriptor = FGaeaFieldDescriptor(),
		float InitialValue = 0.0f);

	bool IsValid() const;
	void Fill(float Value);

	float& AtStorage(int32 X, int32 Y);
	const float& AtStorage(int32 X, int32 Y) const;

	float& AtInterior(int32 X, int32 Y);
	const float& AtInterior(int32 X, int32 Y) const;

	float SampleWorld(const FVector2d& WorldPosition, bool bClampToDomain = true) const;
};
