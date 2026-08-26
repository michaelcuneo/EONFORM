#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainContext.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainGeology.h"
#include "EonformTerrainPhysicalMetrics.h"

struct EONFORMCORE_API FEonformTerrainDerivedDataSettings
{
	int32 GeologySeed = 1337;
	FEonformTerrainGeologySettings Geology;
	FEonformTerrainProcessMaskSettings ProcessMasks;
};

/**
 * Supplies analysis fields on demand without forcing analysis nodes into the
 * main terrain-processing chain. Existing complete field groups are preserved,
 * so explicitly authored analysis nodes act as overrides.
 */
class EONFORMCORE_API FEonformTerrainDerivedData
{
public:
	static bool EnsureContext(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureContext(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	static bool EnsureGeology(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainDerivedDataSettings& Settings = FEonformTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	static bool EnsureProcessMasks(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainDerivedDataSettings& Settings = FEonformTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	/** Ensures only the depression-aware D8 drainage direction field. */
	static bool EnsureDrainage(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureDrainage(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/** Ensures FlowDirection, FlowAccumulation and CatchmentAreaKm2. */
	static bool EnsureFlowAnalysis(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureFlowAnalysis(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/** Ensures flow analysis plus DistanceToOutletKm, without StreamOrder. */
	static bool EnsureHydrologyNetwork(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureHydrologyNetwork(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/** Ensures the complete hydrology product set including StreamOrder. */
	static bool EnsureHydrology(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureHydrology(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/**
	 * Ensures the geology and process masks required by the hydraulic erosion
	 * solver. Drainage is intentionally excluded and remains demand-driven.
	 */
	static bool EnsureHydraulicInputs(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainDerivedDataSettings& Settings = FEonformTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	static bool EnsureHydraulicInputs(
		FEonformTerrainDataset& InOutDataset,
		float HeightScale,
		const FEonformTerrainPhysicalMetrics& PhysicalMetrics,
		const FEonformTerrainDerivedDataSettings& Settings,
		FString* OutError = nullptr);
};