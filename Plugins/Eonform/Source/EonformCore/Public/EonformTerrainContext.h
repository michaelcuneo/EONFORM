#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainPhysicalMetrics.h"

struct EONFORMCORE_API FEonformTerrainProcessMaskSettings
{
	float ThermalTalusAngleDegrees = 34.0f;
	float ThermalRegionality = 0.90f;
	float HydraulicRegionality = 0.85f;
	float RainfallHighlandBias = 0.65f;
	float EvaporationLowlandBias = 0.55f;
};

/** Pure terrain analysis that derives semantic context and process-mask fields from Height. */
class EONFORMCORE_API FEonformTerrainContext
{
public:
	static bool Analyze(
		const FEonformScalarField& Height,
		float HeightScale,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);

	static bool Analyze(
		const FEonformScalarField& Height,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);

	static bool BuildProcessMasks(
		const FEonformScalarField& Height,
		const FEonformTerrainProcessMaskSettings& Settings,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
