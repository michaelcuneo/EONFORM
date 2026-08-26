#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

struct EONFORMCORE_API FEonformThermalErosionSettings
{
	int32 Iterations = 12;
	float TalusAngleDegrees = 34.0f;
	float Strength = 0.35f;

	/** Largest thermal transport feature expressed in terrain samples. */
	float FeatureScaleSamples = 1.0f;

	/** Directional preference for material movement. Zero is isotropic. */
	float Anisotropy = 0.0f;

	/** Fraction of moved material that settles locally/down-slope. */
	float Settling = 0.5f;

	/** Fraction of transported material removed from the terrain. */
	float SedimentRemoval = 0.0f;

	int32 Seed = 1337;
};

/** Runtime-safe thermal erosion used by graph evaluation and legacy terrain generation. */
class EONFORMCORE_API FEonformThermalErosion
{
public:
	static bool ApplyInPlace(
		FEonformScalarField& HeightField,
		float HeightScale,
		const FEonformThermalErosionSettings& Settings,
		const FEonformScalarField* ProcessMask = nullptr,
		const FEonformScalarField* RockHardness = nullptr,
		const FEonformScalarField* AreaMask = nullptr,
		FString* OutError = nullptr);

	static bool ApplyInPlaceWithArrays(
		FEonformScalarField& HeightField,
		float HeightScale,
		const FEonformThermalErosionSettings& Settings,
		const TArray<float>* ProcessMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		FString* OutError = nullptr);
};
