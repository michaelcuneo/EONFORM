#include "TerrainErosion.h"

void FTerrainErosion::ApplyThermal(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainThermalErosionSettings& Settings)
{
	if (!HeightField.IsValid() || Settings.Iterations <= 0 || Settings.Strength <= 0.0f || HeightScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float TalusHeight = FMath::Tan(FMath::DegreesToRadians(Settings.TalusAngleDegrees)) * CellSize / HeightScale;
	const float SafeStrength = FMath::Clamp(Settings.Strength, 0.0f, 1.0f);

	TArray<float> Delta;
	Delta.SetNumZeroed(HeightField.Data.Num());

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
				const float CenterHeight = HeightField.Data[CenterIndex];

				float TotalExcess = 0.0f;
				float ExcessByNeighbor[UE_ARRAY_COUNT(Neighbors)] = {};

				for (int32 NeighborIndex = 0; NeighborIndex < UE_ARRAY_COUNT(Neighbors); ++NeighborIndex)
				{
					const FIntPoint Offset = Neighbors[NeighborIndex];
					const int32 NX = X + Offset.X;
					const int32 NY = Y + Offset.Y;
					const float NeighborHeight = HeightField.At(NX, NY);
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

				const float MaterialToMove = TotalExcess * 0.5f * SafeStrength;
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

		for (int32 Index = 0; Index < HeightField.Data.Num(); ++Index)
		{
			HeightField.Data[Index] += Delta[Index];
		}
	}
}
