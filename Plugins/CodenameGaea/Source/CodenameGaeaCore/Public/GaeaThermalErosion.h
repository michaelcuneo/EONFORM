#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"

struct CODENAMEGAEACORE_API FGaeaThermalErosionSettings
{
	int32 Iterations = 12;
	float TalusAngleDegrees = 34.0f;
	float Strength = 0.35f;
};

/** Runtime-safe thermal erosion used by graph evaluation and legacy terrain generation. */
class CODENAMEGAEACORE_API FGaeaThermalErosion
{
public:
	static bool ApplyInPlace(
		FGaeaScalarField& HeightField,
		float HeightScale,
		const FGaeaThermalErosionSettings& Settings,
		const FGaeaScalarField* ProcessMask = nullptr,
		const FGaeaScalarField* RockHardness = nullptr,
		const FGaeaScalarField* AreaMask = nullptr,
		FString* OutError = nullptr);

	static bool ApplyInPlaceWithArrays(
		FGaeaScalarField& HeightField,
		float HeightScale,
		const FGaeaThermalErosionSettings& Settings,
		const TArray<float>* ProcessMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		FString* OutError = nullptr);
};
