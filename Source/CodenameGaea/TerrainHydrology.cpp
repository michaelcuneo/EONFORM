#include "TerrainHydrology.h"

bool FTerrainHydrology::BuildRiverMask(
	const FTerrainHeightField& HeightField,
	const TArray<float>& FlowAccumulation,
	const FTerrainRiverSettings& Settings,
	TArray<float>& OutRiverMask)
{
	OutRiverMask.Reset();

	if (!HeightField.IsValid() || FlowAccumulation.Num() != HeightField.Data.Num())
	{
		return false;
	}

	const int32 NumCells = HeightField.Data.Num();
	const int32 Resolution = HeightField.Resolution;
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float SafeWidth = FMath::Max(Settings.Width, CellSize);
	const int32 RadiusCells = FMath::Max(1, FMath::CeilToInt(SafeWidth / CellSize));

	float MaxLogFlow = 0.0f;
	TArray<float> NormalizedFlow;
	NormalizedFlow.SetNumZeroed(NumCells);

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float LogFlow = FMath::Loge(1.0f + FMath::Max(FlowAccumulation[Index], 0.0f));
		NormalizedFlow[Index] = LogFlow;
		MaxLogFlow = FMath::Max(MaxLogFlow, LogFlow);
	}

	if (MaxLogFlow <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float Threshold = FMath::Clamp(Settings.FlowThreshold, 0.0f, 1.0f);
	const float Transition = FMath::Max(Settings.ThresholdTransition, 0.001f);
	const float Lower = FMath::Clamp(Threshold - Transition * 0.5f, 0.0f, 1.0f);
	const float Upper = FMath::Clamp(Threshold + Transition * 0.5f, Lower + 0.001f, 1.0f);

	TArray<float> CoreMask;
	CoreMask.SetNumZeroed(NumCells);
	OutRiverMask.SetNumZeroed(NumCells);

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float Flow = NormalizedFlow[Index] / MaxLogFlow;
		const float T = FMath::Clamp((Flow - Lower) / (Upper - Lower), 0.0f, 1.0f);
		CoreMask[Index] = T * T * (3.0f - 2.0f * T);
	}

	const float BankFalloff = FMath::Max(Settings.BankFalloff, 0.1f);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 SourceIndex = HeightField.Index(X, Y);
			const float Core = CoreMask[SourceIndex];
			if (Core <= UE_SMALL_NUMBER)
			{
				continue;
			}

			for (int32 DY = -RadiusCells; DY <= RadiusCells; ++DY)
			{
				for (int32 DX = -RadiusCells; DX <= RadiusCells; ++DX)
				{
					const int32 NX = X + DX;
					const int32 NY = Y + DY;
					if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
					{
						continue;
					}

					const float Distance = FMath::Sqrt(static_cast<float>(DX * DX + DY * DY)) * CellSize;
					if (Distance > SafeWidth)
					{
						continue;
					}

					const float Radial = 1.0f - Distance / SafeWidth;
					const float Influence = Core * FMath::Pow(FMath::Max(Radial, 0.0f), BankFalloff);
					const int32 TargetIndex = HeightField.Index(NX, NY);
					OutRiverMask[TargetIndex] = FMath::Max(OutRiverMask[TargetIndex], Influence);
				}
			}
		}
	}

	return true;
}

void FTerrainHydrology::BuildRiverNetwork(
	const FTerrainHeightField& HeightField,
	const TArray<float>& FlowAccumulation,
	const TArray<int32>& Receiver,
	const FTerrainRiverSettings& Settings,
	TArray<FIntPoint>& OutRiverEdges)
{
	OutRiverEdges.Reset();

	if (!HeightField.IsValid()
		|| FlowAccumulation.Num() != HeightField.Data.Num()
		|| Receiver.Num() != HeightField.Data.Num())
	{
		return;
	}

	float MaxLogFlow = 0.0f;
	TArray<float> LogFlow;
	LogFlow.SetNumZeroed(FlowAccumulation.Num());

	for (int32 Index = 0; Index < FlowAccumulation.Num(); ++Index)
	{
		LogFlow[Index] = FMath::Loge(1.0f + FMath::Max(FlowAccumulation[Index], 0.0f));
		MaxLogFlow = FMath::Max(MaxLogFlow, LogFlow[Index]);
	}

	if (MaxLogFlow <= UE_SMALL_NUMBER)
	{
		return;
	}

	const float Threshold = FMath::Clamp(Settings.FlowThreshold, 0.0f, 1.0f);
	for (int32 Index = 0; Index < FlowAccumulation.Num(); ++Index)
	{
		if (HeightField.Data[Index] < 0.0f)
		{
			continue;
		}

		const float Flow = LogFlow[Index] / MaxLogFlow;
		const int32 Downstream = Receiver[Index];
		if (Flow >= Threshold && Downstream != INDEX_NONE)
		{
			OutRiverEdges.Add(FIntPoint(Index, Downstream));
		}
	}
}

bool FTerrainHydrology::BuildFloodplainMasks(
	const FTerrainHeightField& HeightField,
	const TArray<float>& RiverMask,
	float HeightScale,
	const FTerrainFloodplainSettings& Settings,
	TArray<float>& OutFloodplainMask,
	TArray<float>& OutWetnessMask)
{
	OutFloodplainMask.Reset();
	OutWetnessMask.Reset();

	if (!HeightField.IsValid() || RiverMask.Num() != HeightField.Data.Num() || HeightScale <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float Width = FMath::Max(Settings.Width, CellSize);
	const int32 RadiusCells = FMath::Max(1, FMath::CeilToInt(Width / CellSize));
	const float MaxRiseNormalized = FMath::Max(Settings.MaxRise, 0.0f) / HeightScale;
	const float Falloff = FMath::Max(Settings.Falloff, 0.1f);

	OutFloodplainMask.SetNumZeroed(NumCells);
	OutWetnessMask.SetNumZeroed(NumCells);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 RiverIndex = HeightField.Index(X, Y);
			const float River = RiverMask[RiverIndex];
			if (River < 0.5f)
			{
				continue;
			}

			const float RiverHeight = HeightField.Data[RiverIndex];

			for (int32 DY = -RadiusCells; DY <= RadiusCells; ++DY)
			{
				for (int32 DX = -RadiusCells; DX <= RadiusCells; ++DX)
				{
					const int32 NX = X + DX;
					const int32 NY = Y + DY;
					if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
					{
						continue;
					}

					const float Distance = FMath::Sqrt(static_cast<float>(DX * DX + DY * DY)) * CellSize;
					if (Distance > Width)
					{
						continue;
					}

					const int32 TargetIndex = HeightField.Index(NX, NY);
					const float Rise = FMath::Max(HeightField.Data[TargetIndex] - RiverHeight, 0.0f);
					if (Rise > MaxRiseNormalized)
					{
						continue;
					}

					const float DistanceFactor = FMath::Pow(FMath::Max(1.0f - Distance / Width, 0.0f), Falloff);
					const float HeightFactor = MaxRiseNormalized > UE_SMALL_NUMBER
						? 1.0f - FMath::Clamp(Rise / MaxRiseNormalized, 0.0f, 1.0f)
						: 1.0f;
					const float Floodplain = DistanceFactor * HeightFactor;

					OutFloodplainMask[TargetIndex] = FMath::Max(OutFloodplainMask[TargetIndex], Floodplain);
				}
			}
		}
	}

	const float WetnessStrength = FMath::Clamp(Settings.WetnessStrength, 0.0f, 1.0f);
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float RiverWetness = FMath::Clamp(RiverMask[Index], 0.0f, 1.0f);
		const float FloodWetness = OutFloodplainMask[Index] * WetnessStrength;
		OutWetnessMask[Index] = FMath::Max(RiverWetness, FloodWetness);
	}

	return true;
}

void FTerrainHydrology::CarveRivers(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainRiverSettings& Settings,
	const TArray<float>& RiverMask,
	const TArray<uint8>* TopologyLandMask)
{
	if (!HeightField.IsValid() || RiverMask.Num() != HeightField.Data.Num() || HeightScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 NumCells = HeightField.Data.Num();
	const bool bHasTopology = TopologyLandMask && TopologyLandMask->Num() == NumCells;
	const float NormalizedDepth = FMath::Max(Settings.Depth, 0.0f) / HeightScale;
	const float MinimumLandHeight = FMath::Max(1.0f / HeightScale, 1.0e-6f);
	const float Profile = FMath::Max(Settings.ChannelProfile, 0.1f);

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		if (bHasTopology && (*TopologyLandMask)[Index] == 0)
		{
			continue;
		}

		const float Mask = FMath::Clamp(RiverMask[Index], 0.0f, 1.0f);
		if (Mask <= UE_SMALL_NUMBER)
		{
			continue;
		}

		HeightField.Data[Index] -= NormalizedDepth * FMath::Pow(Mask, Profile);
		if (bHasTopology)
		{
			HeightField.Data[Index] = FMath::Max(HeightField.Data[Index], MinimumLandHeight);
		}
	}
}
