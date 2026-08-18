#include "TerrainGeology.h"

#include "TerrainContext.h"
#include "TerrainNoise.h"
#include "TerrainShaping.h"

void FTerrainGeology::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainContextMaps& Context,
	int32 Seed,
	const FTerrainGeologySettings& Settings,
	FTerrainGeologyMaps& OutGeology)
{
	OutGeology = FTerrainGeologyMaps{};
	if (!HeightField.IsValid() || !Context.IsValidFor(HeightField))
	{
		return;
	}

	const int32 NumCells = HeightField.Data.Num();
	const int32 Resolution = HeightField.Resolution;
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float HalfWorldSize = HeightField.WorldSize * 0.5f;

	OutGeology.RockHardness.SetNumZeroed(NumCells);
	OutGeology.Weathering.SetNumZeroed(NumCells);
	OutGeology.SoilDepth.SetNumZeroed(NumCells);

	FTerrainFractalNoiseSettings GeologyNoiseSettings;
	GeologyNoiseSettings.Frequency = Settings.Frequency;
	GeologyNoiseSettings.Octaves = Settings.Octaves;
	GeologyNoiseSettings.Persistence = 0.5f;
	GeologyNoiseSettings.Lacunarity = 2.0f;

	const FVector2D GeologyOffset = FTerrainNoise::MakeSeedOffset(Seed, 707);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const FVector2D WorldPosition(
				static_cast<float>(X) * CellSize - HalfWorldSize,
				static_cast<float>(Y) * CellSize - HalfWorldSize);

			float Lithology = FTerrainNoise::SampleFractal(WorldPosition, GeologyOffset, GeologyNoiseSettings);
			Lithology = FTerrainShaping::ApplySignedPower(Lithology, Settings.Contrast);
			Lithology = FMath::Clamp(Lithology * 0.5f + 0.5f, 0.0f, 1.0f);

			const float Mountain = Context.Mountain[Index];
			const float Plains = Context.Plains[Index];
			const float Slope = Context.SlopeDegrees[Index];
			const float Concavity = Context.Concavity[Index];

			const float Hardness = FMath::Clamp(
				Lithology + Mountain * Settings.MountainHardnessBias - Plains * Settings.PlainsSoftnessBias,
				0.0f,
				1.0f);
			OutGeology.RockHardness[Index] = Hardness;

			const float Weathering = FMath::Clamp(
				(1.0f - Hardness) * 0.7f + Concavity * 0.2f + (1.0f - FMath::Clamp(Slope / 45.0f, 0.0f, 1.0f)) * 0.1f,
				0.0f,
				1.0f);
			OutGeology.Weathering[Index] = Weathering;

			const float StableSurface = 1.0f - FMath::Clamp(Slope / 35.0f, 0.0f, 1.0f);
			const float SoilDepth = FMath::Clamp(
				Weathering * StableSurface * (0.45f + Plains * 0.35f + Concavity * 0.2f) * Settings.SoilFormationStrength,
				0.0f,
				1.0f);
			OutGeology.SoilDepth[Index] = SoilDepth;
		}
	}
}
