#include "TerrainNoise.h"

FVector2D FTerrainNoise::MakeSeedOffset(int32 Seed, int32 Salt)
{
	return FGaeaTerrainNoise::MakeSeedOffset(Seed, Salt);
}

float FTerrainNoise::SampleFractal(
	const FVector2D& WorldPosition,
	const FVector2D& SeedOffset,
	const FTerrainFractalNoiseSettings& Settings)
{
	return FGaeaTerrainNoise::SampleFractal(WorldPosition, SeedOffset, Settings);
}

float FTerrainNoise::SampleRidged(
	const FVector2D& WorldPosition,
	const FVector2D& SeedOffset,
	const FTerrainFractalNoiseSettings& Settings,
	float Sharpness)
{
	return FGaeaTerrainNoise::SampleRidged(WorldPosition, SeedOffset, Settings, Sharpness);
}
