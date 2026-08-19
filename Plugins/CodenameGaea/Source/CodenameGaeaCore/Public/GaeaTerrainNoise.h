#pragma once

#include "CoreMinimal.h"

struct CODENAMEGAEACORE_API FGaeaFractalNoiseSettings
{
	float Frequency = 0.00055f;
	int32 Octaves = 6;
	float Persistence = 0.5f;
	float Lacunarity = 2.0f;
};

/** Pure deterministic terrain-noise primitives shared by graph and legacy host paths. */
class CODENAMEGAEACORE_API FGaeaTerrainNoise
{
public:
	static FVector2D MakeSeedOffset(int32 Seed, int32 Salt = 0);

	static float SampleFractal(
		const FVector2D& WorldPosition,
		const FVector2D& SeedOffset,
		const FGaeaFractalNoiseSettings& Settings);

	static float SampleRidged(
		const FVector2D& WorldPosition,
		const FVector2D& SeedOffset,
		const FGaeaFractalNoiseSettings& Settings,
		float Sharpness);
};
