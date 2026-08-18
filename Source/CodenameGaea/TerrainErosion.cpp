#include "TerrainErosion.h"

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
}

void FTerrainErosion::ApplyThermal(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainThermalErosionSettings& Settings,
	const TArray<float>* ProcessMask)
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
				if (LocalMask <= UE_SMALL_NUMBER)
				{
					continue;
				}

				const float CenterHeight = HeightField.Data[CenterIndex];
				float TotalExcess = 0.0f;
				float ExcessByNeighbor[UE_ARRAY_COUNT(Neighbors)] = {};

				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const FIntPoint Offset = Neighbors[NeighborIndex];
					const float NeighborHeight = HeightField.At(X + Offset.X, Y + Offset.Y);
					const float DistanceScale = (Offset.X != 0 && Offset.Y != 0) ? UE_SQRT_2 : 1.0f;
					const float LocalTalus = TalusHeight * DistanceScale;
					const float Excess = CenterHeight - NeighborHeight - LocalTalus;

					if (Excess > 0.0f)
					{
						ExcessByNeighbor[NeighborIndex] = Excess;
						TotalExcess += Excess;
					}
				}

				if (TotalExcess <= UE_SMALL_NUMBER)
				{
					continue;
				}

				const float MaterialToMove = TotalExcess * 0.5f * SafeStrength * LocalMask;
				Delta[CenterIndex] -= MaterialToMove;

				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const float Excess = ExcessByNeighbor[NeighborIndex];
					if (Excess <= 0.0f)
					{
						continue;
					}

					const FIntPoint Offset = Neighbors[NeighborIndex];
					const int32 NeighborFlatIndex = HeightField.Index(X + Offset.X, Y + Offset.Y);
					Delta[NeighborFlatIndex] += MaterialToMove * (Excess / TotalExcess);
				}
			}
		}

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			HeightField.Data[Index] += Delta[Index];
		}
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
	const TArray<float>* EvaporationMask)
{
	if (!HeightField.IsValid() || Settings.Iterations <= 0 || HeightScale <= UE_SMALL_NUMBER)
	{
		if (OutFlowAccumulation)
		{
			OutFlowAccumulation->Reset();
		}
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float HeightToSlope = HeightScale / FMath::Max(CellSize, UE_SMALL_NUMBER);

	const float Rainfall = FMath::Max(Settings.Rainfall, 0.0f);
	const float FlowRate = FMath::Clamp(Settings.FlowRate, 0.0f, 1.0f);
	const float CapacityFactor = FMath::Max(Settings.SedimentCapacity, 0.0f);
	const float ErosionRate = FMath::Clamp(Settings.ErosionRate, 0.0f, 1.0f);
	const float DepositionRate = FMath::Clamp(Settings.DepositionRate, 0.0f, 1.0f);
	const float Evaporation = FMath::Clamp(Settings.Evaporation, 0.0f, 1.0f);
	const float MinimumSlope = FMath::Max(Settings.MinimumSlope, 0.0f);

	TArray<float> Water;
	TArray<float> Sediment;
	TArray<float> NextWater;
	TArray<float> NextSediment;
	TArray<float> FlowAccumulation;

	Water.SetNumZeroed(NumCells);
	Sediment.SetNumZeroed(NumCells);
	NextWater.SetNumZeroed(NumCells);
	NextSediment.SetNumZeroed(NumCells);
	FlowAccumulation.SetNumZeroed(NumCells);

	static const FIntPoint Neighbors[] = {
		FIntPoint(-1, 0), FIntPoint(1, 0),
		FIntPoint(0, -1), FIntPoint(0, 1),
		FIntPoint(-1, -1), FIntPoint(1, -1),
		FIntPoint(-1, 1), FIntPoint(1, 1)
	};

	for (int32 Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			Water[Index] += Rainfall * MaskValue(RainfallMask, Index, NumCells);
		}

		FMemory::Memzero(NextWater.GetData(), NumCells * sizeof(float));
		FMemory::Memzero(NextSediment.GetData(), NumCells * sizeof(float));

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = HeightField.Index(X, Y);
				const float AvailableWater = Water[Index];
				const float AvailableSediment = Sediment[Index];

				if (AvailableWater <= UE_SMALL_NUMBER)
				{
					NextSediment[Index] += AvailableSediment;
					continue;
				}

				const float SurfaceHeight = HeightField.Data[Index] + AvailableWater;
				float Drops[UE_ARRAY_COUNT(Neighbors)] = {};
				float TotalDrop = 0.0f;

				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const int32 NX = X + Neighbors[NeighborIndex].X;
					const int32 NY = Y + Neighbors[NeighborIndex].Y;
					if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
					{
						continue;
					}

					const int32 NeighborFlatIndex = HeightField.Index(NX, NY);
					const float NeighborSurface = HeightField.Data[NeighborFlatIndex] + Water[NeighborFlatIndex];
					const float Drop = SurfaceHeight - NeighborSurface;
					if (Drop > 0.0f)
					{
						Drops[NeighborIndex] = Drop;
						TotalDrop += Drop;
					}
				}

				if (TotalDrop <= UE_SMALL_NUMBER || FlowRate <= UE_SMALL_NUMBER)
				{
					NextWater[Index] += AvailableWater;
					NextSediment[Index] += AvailableSediment;
					continue;
				}

				const float WaterToMove = AvailableWater * FlowRate;
				const float SedimentToMove = AvailableSediment * FlowRate;
				NextWater[Index] += AvailableWater - WaterToMove;
				NextSediment[Index] += AvailableSediment - SedimentToMove;

				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					if (Drops[NeighborIndex] <= 0.0f)
					{
						continue;
					}

					const int32 NX = X + Neighbors[NeighborIndex].X;
					const int32 NY = Y + Neighbors[NeighborIndex].Y;
					const int32 NeighborFlatIndex = HeightField.Index(NX, NY);
					const float Share = Drops[NeighborIndex] / TotalDrop;
					const float MovedWater = WaterToMove * Share;

					NextWater[NeighborFlatIndex] += MovedWater;
					NextSediment[NeighborFlatIndex] += SedimentToMove * Share;
					FlowAccumulation[NeighborFlatIndex] += MovedWater;
				}
			}
		}

		Water = MoveTemp(NextWater);
		Sediment = MoveTemp(NextSediment);
		NextWater.SetNumZeroed(NumCells);
		NextSediment.SetNumZeroed(NumCells);

		for (int32 Y = 1; Y < Resolution - 1; ++Y)
		{
			for (int32 X = 1; X < Resolution - 1; ++X)
			{
				const int32 Index = HeightField.Index(X, Y);
				float MaxDownhillDrop = 0.0f;

				for (const FIntPoint& Offset : Neighbors)
				{
					const float DistanceScale = (Offset.X != 0 && Offset.Y != 0) ? UE_SQRT_2 : 1.0f;
					const float Drop = (HeightField.Data[Index] - HeightField.At(X + Offset.X, Y + Offset.Y)) / DistanceScale;
					MaxDownhillDrop = FMath::Max(MaxDownhillDrop, Drop);
				}

				const float PhysicalSlope = FMath::Max(MaxDownhillDrop * HeightToSlope, MinimumSlope);
				const float Capacity = Water[Index] * PhysicalSlope * CapacityFactor;

				if (Sediment[Index] > Capacity)
				{
					const float LocalDeposition = DepositionRate * MaskValue(DepositionMask, Index, NumCells);
					const float Deposit = (Sediment[Index] - Capacity) * LocalDeposition;
					HeightField.Data[Index] += Deposit;
					Sediment[Index] -= Deposit;
				}
				else if (Sediment[Index] < Capacity)
				{
					const float LocalErosion = ErosionRate * MaskValue(ErosionMask, Index, NumCells);
					const float Erode = FMath::Min((Capacity - Sediment[Index]) * LocalErosion, 0.02f);
					HeightField.Data[Index] -= Erode;
					Sediment[Index] += Erode;
				}
			}
		}

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const float LocalEvaporation = Evaporation * MaskValue(EvaporationMask, Index, NumCells);
			Water[Index] *= 1.0f - FMath::Clamp(LocalEvaporation, 0.0f, 1.0f);
		}
	}

	if (OutFlowAccumulation)
	{
		*OutFlowAccumulation = MoveTemp(FlowAccumulation);
	}
}
