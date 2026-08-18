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

class FTerrainHydrology
{
public:
	static bool BuildRiverMask(
		const FTerrainHeightField& HeightField,
		const TArray<float>& FlowAccumulation,
		const FTerrainRiverSettings& Settings,
		TArray<float>& OutRiverMask);

	static void CarveRivers(
		FTerrainHeightField& HeightField,
		float HeightScale,
		const FTerrainRiverSettings& Settings,
		const TArray<float>& RiverMask);
};
