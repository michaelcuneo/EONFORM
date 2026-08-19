#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"

struct CODENAMEGAEACORE_API FGaeaTerrainGeologySettings
{
	float Frequency = 0.000045f;
	int32 Octaves = 3;
	float Contrast = 1.25f;
	float MountainHardnessBias = 0.18f;
	float PlainsSoftnessBias = 0.15f;
	float SoilFormationStrength = 0.65f;
};

/** Pure geology analysis that derives material resistance and soil fields from terrain context. */
class CODENAMEGAEACORE_API FGaeaTerrainGeology
{
public:
	static bool Build(
		const FGaeaScalarField& Height,
		int32 Seed,
		const FGaeaTerrainGeologySettings& Settings,
		FGaeaTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
