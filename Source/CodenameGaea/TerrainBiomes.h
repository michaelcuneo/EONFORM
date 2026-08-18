#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainClimateMaps;
struct FTerrainContextMaps;
struct FTerrainGeologyMaps;

struct FTerrainBiomeSettings
{
	float ForestMoistureThreshold = 0.48f;
	float ForestMaxSlopeDegrees = 32.0f;
	float AridMoistureThreshold = 0.28f;
	float AlpineElevationThreshold = 0.68f;
	float WetlandWetnessThreshold = 0.58f;
};

struct FTerrainBiomeMaps
{
	TArray<float> Forest;
	TArray<float> Grassland;
	TArray<float> Arid;
	TArray<float> Alpine;
	TArray<float> Wetland;
	TArray<float> ExposedRock;
	TArray<float> Snow;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& Forest.Num() == NumCells
			&& Grassland.Num() == NumCells
			&& Arid.Num() == NumCells
			&& Alpine.Num() == NumCells
			&& Wetland.Num() == NumCells
			&& ExposedRock.Num() == NumCells
			&& Snow.Num() == NumCells;
	}
};

class FTerrainBiomes
{
public:
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainContextMaps& Context,
		const FTerrainClimateMaps& Climate,
		const FTerrainGeologyMaps& Geology,
		const TArray<float>& WetnessMask,
		const TArray<float>& RiverMask,
		const FTerrainBiomeSettings& Settings,
		FTerrainBiomeMaps& OutBiomes);
};
