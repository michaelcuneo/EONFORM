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

/**
 * Optional whole-world contract for evaluating Terrain Context on a regional
 * Height field. The reference lattice fixes physical spacing and world-edge
 * behaviour while the supplied extrema keep normalized semantic fields exactly
 * equivalent to a full-world evaluation.
 */
struct EONFORMCORE_API FEonformTerrainContextEvaluationScope
{
	FIntPoint ReferenceResolution = FIntPoint::ZeroValue;
	FEonformGridDomain ReferenceDomain;
	bool bUseGlobalHeightRange = false;
	float GlobalMinimumHeight = 0.0f;
	float GlobalMaximumHeight = 0.0f;

	bool IsRegional() const
	{
		return ReferenceResolution.X > 1
			&& ReferenceResolution.Y > 1
			&& ReferenceDomain.IsValid();
	}
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

	/**
	 * Authoritative regional Terrain Context evaluation. The caller supplies the
	 * exact full-world extrema and reference lattice; neighbourhood derivatives
	 * consume Height's scheduler-provided guard band while preserving the legacy
	 * clamp/neighbor-count behaviour at actual world boundaries.
	 */
	static bool Analyze(
		const FEonformScalarField& Height,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		const FEonformTerrainContextEvaluationScope& EvaluationScope,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);

	static bool BuildProcessMasks(
		const FEonformScalarField& Height,
		const FEonformTerrainProcessMaskSettings& Settings,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
