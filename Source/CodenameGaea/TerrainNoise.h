#pragma once

#include "CoreMinimal.h"

struct FTerrainFractalNoiseSettings
{
	float Frequency = 0.00055f;
	int32 Octaves = 6;
	float Persistence = 0.5f;
	float Lacunarity = 2.0f;
};

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
