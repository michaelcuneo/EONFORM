#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"

struct CODENAMEGAEACORE_API FGaeaHydraulicErosionSettings
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

struct CODENAMEGAEACORE_API FGaeaHydraulicErosionResult
{
	FGaeaScalarField Height;
	FGaeaScalarField Wear;
	FGaeaScalarField Deposits;
	FGaeaScalarField Flow;

	bool IsValid() const;
};

/** Runtime-safe hydraulic erosion implementation used by both graph evaluation and legacy compatibility code. */
class CODENAMEGAEACORE_API FGaeaHydraulicErosion
{
public:
	static bool ApplyInPlace(
		FGaeaScalarField& HeightField,
		float HeightScale,
		const FGaeaHydraulicErosionSettings& Settings,
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
		const FGaeaScalarField& InputHeight,
		float HeightScale,
		const FGaeaHydraulicErosionSettings& Settings,
		FGaeaHydraulicErosionResult& OutResult,
		const TArray<float>* RainfallMask = nullptr,
		const TArray<float>* ErosionMask = nullptr,
		const TArray<float>* DepositionMask = nullptr,
		const TArray<float>* EvaporationMask = nullptr,
		const TArray<float>* RockHardness = nullptr,
		const TArray<float>* SoilDepth = nullptr,
		const TArray<float>* AreaMask = nullptr,
		const TArray<float>* InitialSediment = nullptr);

	static bool Evaluate(
		const FGaeaScalarField& InputHeight,
		float HeightScale,
		const FGaeaHydraulicErosionSettings& Settings,
		FGaeaHydraulicErosionResult& OutResult,
		const FGaeaScalarField* RainfallMask = nullptr,
		const FGaeaScalarField* ErosionMask = nullptr,
		const FGaeaScalarField* DepositionMask = nullptr,
		const FGaeaScalarField* EvaporationMask = nullptr,
		const FGaeaScalarField* RockHardness = nullptr,
		const FGaeaScalarField* SoilDepth = nullptr,
		const FGaeaScalarField* AreaMask = nullptr,
		const FGaeaScalarField* InitialSediment = nullptr);
};
