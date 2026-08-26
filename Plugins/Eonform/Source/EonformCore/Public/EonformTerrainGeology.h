#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"

struct EONFORMCORE_API FEonformTerrainGeologySettings
{
	float Frequency = 0.000045f;
	int32 Octaves = 3;
	float Contrast = 1.25f;
	float MountainHardnessBias = 0.18f;
	float PlainsSoftnessBias = 0.15f;
	float SoilFormationStrength = 0.65f;
};

/** Pure geology analysis that derives material resistance and soil fields from terrain context. */
class EONFORMCORE_API FEonformTerrainGeology
{
public:
	static bool Build(
		const FEonformScalarField& Height,
		int32 Seed,
		const FEonformTerrainGeologySettings& Settings,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
