#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainContext.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainGeology.h"
#include "GaeaTerrainPhysicalMetrics.h"

struct CODENAMEGAEACORE_API FGaeaTerrainDerivedDataSettings
{
	int32 GeologySeed = 1337;
	FGaeaTerrainGeologySettings Geology;
	FGaeaTerrainProcessMaskSettings ProcessMasks;
};

/**
 * Supplies analysis fields on demand without forcing analysis nodes into the
 * main terrain-processing chain. Existing complete field groups are preserved,
 * so explicitly authored analysis nodes act as overrides.
 */
class CODENAMEGAEACORE_API FGaeaTerrainDerivedData
{
public:
	static bool EnsureContext(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureContext(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	static bool EnsureGeology(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainDerivedDataSettings& Settings = FGaeaTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	static bool EnsureProcessMasks(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainDerivedDataSettings& Settings = FGaeaTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	/** Ensures only the depression-aware D8 drainage direction field. */
	static bool EnsureDrainage(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureDrainage(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/** Ensures FlowDirection, FlowAccumulation and CatchmentAreaKm2. */
	static bool EnsureFlowAnalysis(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureFlowAnalysis(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/** Ensures flow analysis plus DistanceToOutletKm, without StreamOrder. */
	static bool EnsureHydrologyNetwork(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureHydrologyNetwork(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/** Ensures the complete hydrology product set including StreamOrder. */
	static bool EnsureHydrology(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		FString* OutError = nullptr);

	static bool EnsureHydrology(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		FString* OutError = nullptr);

	/**
	 * Ensures the geology and process masks required by the hydraulic erosion
	 * solver. Drainage is intentionally excluded and remains demand-driven.
	 */
	static bool EnsureHydraulicInputs(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainDerivedDataSettings& Settings = FGaeaTerrainDerivedDataSettings(),
		FString* OutError = nullptr);

	static bool EnsureHydraulicInputs(
		FGaeaTerrainDataset& InOutDataset,
		float HeightScale,
		const FGaeaTerrainPhysicalMetrics& PhysicalMetrics,
		const FGaeaTerrainDerivedDataSettings& Settings,
		FString* OutError = nullptr);
};