#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainPhysicalMetrics.h"

struct CODENAMEGAEACORE_API FGaeaTerrainProcessMaskSettings
{
	float ThermalTalusAngleDegrees = 34.0f;
	float ThermalRegionality = 0.90f;
	float HydraulicRegionality = 0.85f;
	float RainfallHighlandBias = 0.65f;
	float EvaporationLowlandBias = 0.55f;
};

/** Pure terrain analysis that derives semantic context and process-mask fields from Height. */
class CODENAMEGAEACORE_API FGaeaTerrainContext
{
public:
	static bool Analyze(
		const FGaeaScalarField& Height,
		float HeightScale,
		FGaeaTerrainDataset& InOutDataset,
		FString* OutError = nullptr);

	static bool Analyze(
		const FGaeaScalarField& Height,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FGaeaTerrainDataset& InOutDataset,
		FString* OutError = nullptr);

	static bool BuildProcessMasks(
		const FGaeaScalarField& Height,
		const FGaeaTerrainProcessMaskSettings& Settings,
		FGaeaTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
