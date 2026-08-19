#include "TerrainErosion.h"

#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaThermalErosion.h"

namespace
{
	FGaeaThermalErosionSettings ToCoreSettings(const FTerrainThermalErosionSettings& Settings)
	{
		FGaeaThermalErosionSettings Core;
		Core.Iterations = Settings.Iterations;
		Core.TalusAngleDegrees = Settings.TalusAngleDegrees;
		Core.Strength = Settings.Strength;
		return Core;
	}

	FGaeaHydraulicErosionSettings ToCoreSettings(const FTerrainHydraulicErosionSettings& Settings)
	{
		FGaeaHydraulicErosionSettings Core;
		Core.Iterations = Settings.Iterations;
		Core.Rainfall = Settings.Rainfall;
		Core.FlowRate = Settings.FlowRate;
		Core.SedimentCapacity = Settings.SedimentCapacity;
		Core.ErosionRate = Settings.ErosionRate;
		Core.DepositionRate = Settings.DepositionRate;
		Core.Evaporation = Settings.Evaporation;
		Core.MinimumSlope = Settings.MinimumSlope;
		return Core;
	}
}

void FTerrainErosion::ApplyThermal(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainThermalErosionSettings& Settings,
	const TArray<float>* ProcessMask,
	const TArray<float>* RockHardness)
{
	FGaeaThermalErosion::ApplyInPlaceWithArrays(
		HeightField.GetGaeaField(),
		HeightScale,
		ToCoreSettings(Settings),
		ProcessMask,
		RockHardness);
}

void FTerrainErosion::ApplyHydraulic(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainHydraulicErosionSettings& Settings,
	TArray<float>* OutFlowAccumulation,
	const TArray<float>* RainfallMask,
	const TArray<float>* ErosionMask,
	const TArray<float>* DepositionMask,
	const TArray<float>* EvaporationMask,
	const TArray<float>* RockHardness,
	const TArray<float>* SoilDepth,
	TArray<float>* OutWear,
	TArray<float>* OutDeposits)
{
	FGaeaHydraulicErosion::ApplyInPlace(
		HeightField.GetGaeaField(), HeightScale, ToCoreSettings(Settings), OutFlowAccumulation,
		RainfallMask, ErosionMask, DepositionMask, EvaporationMask, RockHardness, SoilDepth, OutWear, OutDeposits);
}

bool FTerrainErosion::EvaluateHydraulic(
	const FTerrainHeightField& InputHeightField,
	float HeightScale,
	const FTerrainHydraulicErosionSettings& Settings,
	FTerrainHydraulicErosionResult& OutResult,
	const TArray<float>* RainfallMask,
	const TArray<float>* ErosionMask,
	const TArray<float>* DepositionMask,
	const TArray<float>* EvaporationMask,
	const TArray<float>* RockHardness,
	const TArray<float>* SoilDepth)
{
	FGaeaHydraulicErosionResult CoreResult;
	if (!FGaeaHydraulicErosion::EvaluateWithArrays(
		InputHeightField.GetGaeaField(), HeightScale, ToCoreSettings(Settings), CoreResult,
		RainfallMask, ErosionMask, DepositionMask, EvaporationMask, RockHardness, SoilDepth))
	{
		OutResult = FTerrainHydraulicErosionResult{};
		return false;
	}
	OutResult.Height = MoveTemp(CoreResult.Height);
	OutResult.Wear = MoveTemp(CoreResult.Wear);
	OutResult.Deposits = MoveTemp(CoreResult.Deposits);
	OutResult.Flow = MoveTemp(CoreResult.Flow);
	return OutResult.IsValid();
}
