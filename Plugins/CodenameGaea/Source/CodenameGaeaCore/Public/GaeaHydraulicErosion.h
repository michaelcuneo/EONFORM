#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"

struct CODENAMEGAEACORE_API FGaeaHydraulicErosionSettings
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
		TArray<float>* OutDeposits = nullptr);

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
		const TArray<float>* SoilDepth = nullptr);

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
		const FGaeaScalarField* SoilDepth = nullptr);
};
