#pragma once

#include "CoreMinimal.h"
#include "EonformGridDomain.h"

enum class EEonformFieldUnit : uint8
{
	Unitless,
	// Compatibility alias for scalar/unitless diagnostic fields.
	Scalar = Unitless,
	Normalized,
	Centimeters,
	Meters,
	Kilometers,
	SquareKilometers,
	Degrees,
	Celsius
};

enum class EEonformInterpolation : uint8
{
	Nearest,
	Bilinear
};

struct EONFORMCORE_API FEonformFieldDescriptor
{
	FName Name = NAME_None;
	EEonformFieldUnit Unit = EEonformFieldUnit::Unitless;
	EEonformInterpolation Interpolation = EEonformInterpolation::Bilinear;
};

/**
 * Dense scalar values sampled over an FEonformGridDomain.
 * Storage includes the domain guard band when BorderSamples > 0.
 */
struct EONFORMCORE_API FEonformScalarField
{
	FEonformGridDomain Domain;
	FEonformFieldDescriptor Descriptor;
	TArray<float> Values;

	void Initialize(
		const FEonformGridDomain& InDomain,
		const FEonformFieldDescriptor& InDescriptor = FEonformFieldDescriptor(),
		float InitialValue = 0.0f);

	bool IsValid() const;
	void Fill(float Value);

	float& AtStorage(int32 X, int32 Y);
	const float& AtStorage(int32 X, int32 Y) const;

	float& AtInterior(int32 X, int32 Y);
	const float& AtInterior(int32 X, int32 Y) const;

	float SampleWorld(const FVector2d& WorldPosition, bool bClampToDomain = true) const;
};
