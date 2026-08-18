#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainContextMaps
{
	TArray<float> Elevation;
	TArray<float> SlopeDegrees;
	TArray<float> Concavity;
	TArray<float> Convexity;
	TArray<float> Mountain;
	TArray<float> Foothill;
	TArray<float> Plains;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& Elevation.Num() == NumCells
			&& SlopeDegrees.Num() == NumCells
			&& Concavity.Num() == NumCells
			&& Convexity.Num() == NumCells
			&& Mountain.Num() == NumCells
			&& Foothill.Num() == NumCells
			&& Plains.Num() == NumCells;
	}
};

struct FTerrainProcessMaskSettings
{
	float ThermalRegionality = 0.85f;
	float HydraulicRegionality = 0.8f;
	float RainfallHighlandBias = 0.65f;
	float EvaporationLowlandBias = 0.55f;
};

struct FTerrainProcessMasks
{
	TArray<float> Thermal;
	TArray<float> Rainfall;
	TArray<float> HydraulicErosion;
	TArray<float> Deposition;
	TArray<float> Evaporation;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& Thermal.Num() == NumCells
			&& Rainfall.Num() == NumCells
			&& HydraulicErosion.Num() == NumCells
			&& Deposition.Num() == NumCells
			&& Evaporation.Num() == NumCells;
	}
};

class FTerrainContext
{
public:
	static void Analyze(
		const FTerrainHeightField& HeightField,
		float HeightScale,
		const TArray<float>& MountainMask,
		const TArray<float>& FoothillMask,
		const TArray<float>& PlainsMask,
		FTerrainContextMaps& OutContext);

	static void BuildProcessMasks(
		const FTerrainContextMaps& Context,
		const FTerrainHeightField& HeightField,
		float ThermalTalusAngleDegrees,
		const FTerrainProcessMaskSettings& Settings,
		FTerrainProcessMasks& OutMasks);
};
