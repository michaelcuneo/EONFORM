#include "TerrainNoise.h"

FVector2D FTerrainNoise::MakeSeedOffset(int32 Seed, int32 Salt)
{
	return FEonformTerrainNoise::MakeSeedOffset(Seed, Salt);
}

float FTerrainNoise::SampleFractal(
	const FVector2D& WorldPosition,
	const FVector2D& SeedOffset,
	const FTerrainFractalNoiseSettings& Settings)
{
	return FEonformTerrainNoise::SampleFractal(WorldPosition, SeedOffset, Settings);
}

float FTerrainNoise::SampleRidged(
	const FVector2D& WorldPosition,
	const FVector2D& SeedOffset,
	const FTerrainFractalNoiseSettings& Settings,
	float Sharpness)
{
	return FEonformTerrainNoise::SampleRidged(WorldPosition, SeedOffset, Settings, Sharpness);
}
