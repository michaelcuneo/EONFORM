#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainContextMaps;

struct FTerrainClimateSettings
{
	float PrevailingWindDirectionDegrees = 235.0f;
	float BaseTemperatureC = 18.0f;
	float LapseRateCPerKm = 6.5f;
	float BaseHumidity = 0.62f;
	float OrographicStrength = 0.78f;
	float RainShadowStrength = 0.68f;
	float MoistureRecovery = 0.08f;
	float SnowTemperatureC = 1.5f;
};

struct FTerrainClimateMaps
{
	TArray<float> TemperatureC;
	TArray<float> Precipitation;
	TArray<float> Humidity;
	TArray<float> EvaporationPotential;
	TArray<float> SnowPotential;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& TemperatureC.Num() == NumCells
			&& Precipitation.Num() == NumCells
			&& Humidity.Num() == NumCells
			&& EvaporationPotential.Num() == NumCells
			&& SnowPotential.Num() == NumCells;
	}
};

class FTerrainClimate
{
public:
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainContextMaps& Context,
		float HeightScale,
		const FTerrainClimateSettings& Settings,
		FTerrainClimateMaps& OutClimate);
};
