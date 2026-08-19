#include "TerrainErosion.h"

#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainFieldNames.h"

namespace
{
	float MaskValue(const TArray<float>* Mask, int32 Index, int32 ExpectedNum)
	{
		if (!Mask || Mask->Num() != ExpectedNum)
		{
			return 1.0f;
		}
		return FMath::Clamp((*Mask)[Index], 0.0f, 1.0f);
	}

	float HardnessResistance(const TArray<float>* RockHardness, int32 Index, int32 ExpectedNum)
	{
		const float Hardness = RockHardness && RockHardness->Num() == ExpectedNum
			? FMath::Clamp((*RockHardness)[Index], 0.0f, 1.0f)
			: 0.5f;
		return FMath::Lerp(1.0f, 0.18f, Hardness);
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
	if (!HeightField.IsValid() || Settings.Iterations <= 0 || Settings.Strength <= 0.0f || HeightScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float TalusHeight = FMath::Tan(FMath::DegreesToRadians(Settings.TalusAngleDegrees)) * CellSize / HeightScale;
	const float SafeStrength = FMath::Clamp(Settings.Strength, 0.0f, 1.0f);

	TArray<float> Delta;
	Delta.SetNumZeroed(NumCells);

	static const FIntPoint Neighbors[] = {
		FIntPoint(-1, 0), FIntPoint(1, 0),
		FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1),
		FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(float));
		for (int32 Y = 1; Y < Resolution - 1; ++Y)
		{
			for (int32 X = 1; X < Resolution - 1; ++X)
			{
				const int32 CenterIndex = HeightField.Index(X, Y);
				const float LocalMask = MaskValue(ProcessMask, CenterIndex, NumCells);
				if (LocalMask <= UE_SMALL_NUMBER) continue;

				const float CenterHeight = HeightField.Data[CenterIndex];
				float TotalExcess = 0.0f;
				float ExcessByNeighbor[UE_ARRAY_COUNT(Neighbors)] = {};
				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const FIntPoint Offset = Neighbors[NeighborIndex];
					const float NeighborHeight = HeightField.At(X + Offset.X, Y + Offset.Y);
					const float DistanceScale = (Offset.X != 0 && Offset.Y != 0) ? UE_SQRT_2 : 1.0f;
					const float Excess = CenterHeight - NeighborHeight - TalusHeight * DistanceScale;
					if (Excess > 0.0f) { ExcessByNeighbor[NeighborIndex] = Excess; TotalExcess += Excess; }
				}
				if (TotalExcess <= UE_SMALL_NUMBER) continue;

				const float Resistance = HardnessResistance(RockHardness, CenterIndex, NumCells);
				const float MaterialToMove = TotalExcess * 0.5f * SafeStrength * LocalMask * Resistance;
				Delta[CenterIndex] -= MaterialToMove;
				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const float Excess = ExcessByNeighbor[NeighborIndex];
					if (Excess <= 0.0f) continue;
					const FIntPoint Offset = Neighbors[NeighborIndex];
					Delta[HeightField.Index(X + Offset.X, Y + Offset.Y)] += MaterialToMove * (Excess / TotalExcess);
				}
			}
		}
		for (int32 Index = 0; Index < NumCells; ++Index) HeightField.Data[Index] += Delta[Index];
	}
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
