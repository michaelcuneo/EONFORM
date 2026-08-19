#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainThermalErosionSettings
{
	int32 Iterations = 12;
	float TalusAngleDegrees = 34.0f;
	float Strength = 0.35f;
};

struct FTerrainHydraulicErosionSettings
{
	int32 Iterations = 24;
	float Rainfall = 0.01f;
	float FlowRate = 0.55f;
	float SedimentCapacity = 0.7f;
	float ErosionRate = 0.18f;
	float DepositionRate = 0.12f;
	float Evaporation = 0.08f;
	float MinimumSlope = 0.01f;
};

class FTerrainErosion
{
public:
	static void ApplyThermal(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainThermalErosionSettings& Settings,
		const TArray<float>* ProcessMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		const TArray<uint8>* TopologyLandMask = nullptr);

	static void ApplyHydraulic(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainHydraulicErosionSettings& Settings,
		TArray<float>* OutFlowAccumulation = nullptr,
		const TArray<float>* RainfallMask = nullptr,
		const TArray<float>* ErosionMask = nullptr,
		const TArray<float>* DepositionMask = nullptr,
		const TArray<float>* EvaporationMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		const TArray<float>* SoilDepth = nullptr,
		const TArray<uint8>* TopologyLandMask = nullptr);
};
