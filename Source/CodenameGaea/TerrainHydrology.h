#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainRiverSettings
{
	float FlowThreshold = 0.58f;
	float ThresholdTransition = 0.14f;
	float Width = 850.0f;
	float Depth = 180.0f;
	float BankFalloff = 1.8f;
	float ChannelProfile = 1.35f;
};

struct FTerrainFloodplainSettings
{
	float Width = 1800.0f;
	float MaxRise = 260.0f;
	float Falloff = 1.6f;
	float WetnessStrength = 0.7f;
};

class FTerrainHydrology
{
public:
	static bool BuildRiverMask(
		const FTerrainHeightField& HeightField,
		const TArray<float>& FlowAccumulation,
		const FTerrainRiverSettings& Settings,
		TArray<float>& OutRiverMask);

	static void BuildRiverNetwork(
		const FTerrainHeightField& HeightField,
		const TArray<float>& FlowAccumulation,
		const TArray<int32>& Receiver,
		const FTerrainRiverSettings& Settings,
		TArray<FIntPoint>& OutRiverEdges);

	static bool BuildFloodplainMasks(
		const FTerrainHeightField& HeightField,
		const TArray<float>& RiverMask,
		float HeightScale,
		const FTerrainFloodplainSettings& Settings,
		TArray<float>& OutFloodplainMask,
		TArray<float>& OutWetnessMask);

	static void CarveRivers(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainRiverSettings& Settings,
		const TArray<float>& RiverMask,
		const TArray<uint8>* TopologyLandMask = nullptr);
};
