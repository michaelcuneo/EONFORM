#include "EonformTerrainNoise.h"

FVector2D FEonformTerrainNoise::MakeSeedOffset(int32 Seed, int32 Salt)
{
	FRandomStream RandomStream(HashCombineFast(GetTypeHash(Seed), GetTypeHash(Salt)));
	return FVector2D(
		RandomStream.FRandRange(-100000.0f, 100000.0f),
		RandomStream.FRandRange(-100000.0f, 100000.0f));
}

float FEonformTerrainNoise::SampleFractal(
	const FVector2D& WorldPosition,
	const FVector2D& SeedOffset,
	const FEonformFractalNoiseSettings& Settings)
{
	float Amplitude = 1.0f;
	float LocalFrequency = Settings.Frequency;
	float Value = 0.0f;
	float AmplitudeSum = 0.0f;

	for (int32 Octave = 0; Octave < FMath::Max(Settings.Octaves, 1); ++Octave)
	{
		const FVector2D SamplePosition = (WorldPosition + SeedOffset) * LocalFrequency;
		Value += FMath::PerlinNoise2D(SamplePosition) * Amplitude;
		AmplitudeSum += Amplitude;
		Amplitude *= Settings.Persistence;
		LocalFrequency *= Settings.Lacunarity;
	}

	return AmplitudeSum > UE_SMALL_NUMBER ? Value / AmplitudeSum : 0.0f;
}

float FEonformTerrainNoise::SampleRidged(
	const FVector2D& WorldPosition,
	const FVector2D& SeedOffset,
	const FEonformFractalNoiseSettings& Settings,
	float Sharpness)
{
	float Amplitude = 1.0f;
	float LocalFrequency = Settings.Frequency;
	float Value = 0.0f;
	float AmplitudeSum = 0.0f;

	for (int32 Octave = 0; Octave < FMath::Max(Settings.Octaves, 1); ++Octave)
	{
		const FVector2D SamplePosition = (WorldPosition + SeedOffset) * LocalFrequency;
		const float Noise = FMath::PerlinNoise2D(SamplePosition);
		const float Ridge = FMath::Pow(1.0f - FMath::Abs(Noise), FMath::Max(Sharpness, 0.01f));
		Value += Ridge * Amplitude;
		AmplitudeSum += Amplitude;
		Amplitude *= Settings.Persistence;
		LocalFrequency *= Settings.Lacunarity;
	}

	return AmplitudeSum > UE_SMALL_NUMBER ? Value / AmplitudeSum : 0.0f;
}
