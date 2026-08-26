#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainNoise.h"

using FTerrainFractalNoiseSettings = FEonformFractalNoiseSettings;

class FTerrainNoise
{
public:
	static FVector2D MakeSeedOffset(int32 Seed, int32 Salt = 0);

	static float SampleFractal(
		const FVector2D& WorldPosition,
		const FVector2D& SeedOffset,
		const FTerrainFractalNoiseSettings& Settings);

	static float SampleRidged(
		const FVector2D& WorldPosition,
		const FVector2D& SeedOffset,
		const FTerrainFractalNoiseSettings& Settings,
		float Sharpness);
};
