#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainThermalErosionSettings
{
	int32 Iterations = 12;
	float TalusAngleDegrees = 34.0f;
	float Strength = 0.35f;
};

class FTerrainErosion
{
public:
	static void ApplyThermal(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainThermalErosionSettings& Settings);
};
