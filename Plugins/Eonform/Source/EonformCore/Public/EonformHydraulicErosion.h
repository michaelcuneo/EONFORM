#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

struct EONFORMCORE_API FEonformHydraulicErosionSettings
{
	int32 Iterations = 24;
	float RockSoftness = 0.0f;
	float Strength = 1.0f;

	float Downcutting = 0.5f;
	float Inhibition = 0.0f;
	float BaseLevel = -1.0f;

	float FeatureScale = 1.0f;

	float Debris = 0.5f;
	float Volume = 1.0f;
	float SedimentRemoval = 0.0f;

	FName SelectiveProcessing = TEXT("None");
	int32 Seed = 1337;
	bool bAggressiveMode = false;
	bool bDeterministic = true;

	/**
	 * Uses EONFORM's drainage-network/stream-power solver. The original local
	 * water-cell solver remains available as an explicit compatibility fallback.
	 */
	bool bAdvancedFlowSolver = true;

	/** Optional physical overrides supplied by graph evaluation. Zero preserves legacy domain units. */
	double PhysicalSampleSpacingMeters = 0.0;
	double PhysicalElevationScaleMeters = 0.0;

	// Internal solver coefficients. These remain implementation details rather
	// than user-facing graph controls.
	float Rainfall = 0.01f;
	float FlowRate = 0.55f;
	float SedimentCapacity = 0.7f;
	float ErosionRate = 0.18f;
	float DepositionRate = 0.12f;
	float Evaporation = 0.08f;
	float MinimumSlope = 0.01f;
};

struct EONFORMCORE_API FEonformHydraulicErosionResult
{
	FEonformScalarField Height;
	FEonformScalarField Wear;
	FEonformScalarField Deposits;
	FEonformScalarField Flow;

	bool IsValid() const;
};

/** Runtime-safe hydraulic erosion implementation used by both graph evaluation and legacy compatibility code. */
class EONFORMCORE_API FEonformHydraulicErosion
{
public:
	static bool ApplyInPlace(
		FEonformScalarField& HeightField,
		float HeightScale,
		const FEonformHydraulicErosionSettings& Settings,
		TArray<float>* OutFlowAccumulation = nullptr,
		const TArray<float>* RainfallMask = nullptr,
		const TArray<float>* ErosionMask = nullptr,
		const TArray<float>* DepositionMask = nullptr,
		const TArray<float>* EvaporationMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		const TArray<float>* SoilDepth = nullptr,
		TArray<float>* OutWear = nullptr,
		TArray<float>* OutDeposits = nullptr,
		const TArray<float>* AreaMask = nullptr,
		const TArray<float>* InitialSediment = nullptr);

	static bool EvaluateWithArrays(
		const FEonformScalarField& InputHeight,
		float HeightScale,
		const FEonformHydraulicErosionSettings& Settings,
		FEonformHydraulicErosionResult& OutResult,
		const TArray<float>* RainfallMask = nullptr,
		const TArray<float>* ErosionMask = nullptr,
		const TArray<float>* DepositionMask = nullptr,
		const TArray<float>* EvaporationMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		const TArray<float>* SoilDepth = nullptr,
		const TArray<float>* AreaMask = nullptr,
		const TArray<float>* InitialSediment = nullptr);

	static bool Evaluate(
		const FEonformScalarField& InputHeight,
		float HeightScale,
		const FEonformHydraulicErosionSettings& Settings,
		FEonformHydraulicErosionResult& OutResult,
		const FEonformScalarField* RainfallMask = nullptr,
		const FEonformScalarField* ErosionMask = nullptr,
		const FEonformScalarField* DepositionMask = nullptr,
		const FEonformScalarField* EvaporationMask = nullptr,
		const FEonformScalarField* RockHardness = nullptr,
		const FEonformScalarField* SoilDepth = nullptr,
		const FEonformScalarField* AreaMask = nullptr,
		const FEonformScalarField* InitialSediment = nullptr);
};
