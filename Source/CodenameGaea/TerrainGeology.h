#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainContextMaps;

struct FTerrainGeologySettings
{
	float Frequency = 0.000045f;
	int32 Octaves = 3;
	float Contrast = 1.25f;
	float MountainHardnessBias = 0.18f;
	float PlainsSoftnessBias = 0.15f;
	float SoilFormationStrength = 0.65f;
};

struct FTerrainGeologyMaps
{
	TArray<float> RockHardness;
	TArray<float> Weathering;
	TArray<float> SoilDepth;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& RockHardness.Num() == NumCells
			&& Weathering.Num() == NumCells
			&& SoilDepth.Num() == NumCells;
	}
};

class FTerrainGeology
{
public:
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainContextMaps& Context,
		int32 Seed,
		const FTerrainGeologySettings& Settings,
		FTerrainGeologyMaps& OutGeology);
};
