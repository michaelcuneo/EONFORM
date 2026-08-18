#include "TerrainLandmass.h"

namespace
{
	constexpr float DiagonalDistance = 1.41421356237f;
	constexpr float LargeDistance = 1.0e30f;

	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	void KeepLargestLandComponent(TArray<uint8>& Land, int32 Resolution)
	{
		const int32 NumCells = Land.Num();
		TArray<uint8> Visited;
		Visited.SetNumZeroed(NumCells);
		TArray<int32> Queue;
		TArray<int32> Component;
		TArray<int32> Largest;

		for (int32 Start = 0; Start < NumCells; ++Start)
		{
			if (Land[Start] == 0 || Visited[Start] != 0)
			{
				continue;
			}

			Queue.Reset();
			Component.Reset();
			Queue.Add(Start);
			Visited[Start] = 1;
			int32 Head = 0;

			while (Head < Queue.Num())
			{
				const int32 Index = Queue[Head++];
				Component.Add(Index);
				const int32 X = Index % Resolution;
				const int32 Y = Index / Resolution;

				for (int32 OY = -1; OY <= 1; ++OY)
				{
					for (int32 OX = -1; OX <= 1; ++OX)
					{
						if (OX == 0 && OY == 0)
						{
							continue;
						}
						const int32 NX = X + OX;
						const int32 NY = Y + OY;
						if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
						{
							continue;
						}
						const int32 Neighbor = NY * Resolution + NX;
						if (Land[Neighbor] != 0 && Visited[Neighbor] == 0)
						{
							Visited[Neighbor] = 1;
							Queue.Add(Neighbor);
						}
					}
				}
			}

			if (Component.Num() > Largest.Num())
			{
				Largest = Component;
			}
		}

		Land.Init(0, NumCells);
		for (const int32 Index : Largest)
		{
			Land[Index] = 1;
		}
	}

	bool IsCoastCell(const TArray<uint8>& Land, int32 Resolution, int32 X, int32 Y)
	{
		const uint8 CellClass = Land[Y * Resolution + X];
		static constexpr int32 DX[4] = { -1, 1, 0, 0 };
		static constexpr int32 DY[4] = { 0, 0, -1, 1 };
		for (int32 Direction = 0; Direction < 4; ++Direction)
		{
			const int32 NX = X + DX[Direction];
			const int32 NY = Y + DY[Direction];
			if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution)
			{
				continue;
			}
			if (Land[NY * Resolution + NX] != CellClass)
			{
				return true;
			}
		}
		return false;
	}

	void BuildCoastDistance(const TArray<uint8>& Land, int32 Resolution, float CellSize, TArray<float>& OutDistance)
	{
		const int32 NumCells = Land.Num();
		OutDistance.Init(LargeDistance, NumCells);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				if (IsCoastCell(Land, Resolution, X, Y))
				{
					OutDistance[Y * Resolution + X] = 0.0f;
				}
			}
		}

		const float Cardinal = CellSize;
		const float Diagonal = CellSize * DiagonalDistance;
		auto Relax = [&OutDistance](int32 Index, int32 Neighbor, float Cost)
		{
			OutDistance[Index] = FMath::Min(OutDistance[Index], OutDistance[Neighbor] + Cost);
		};

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 I = Y * Resolution + X;
				if (X > 0) Relax(I, I - 1, Cardinal);
				if (Y > 0)
				{
					Relax(I, I - Resolution, Cardinal);
					if (X > 0) Relax(I, I - Resolution - 1, Diagonal);
					if (X + 1 < Resolution) Relax(I, I - Resolution + 1, Diagonal);
				}
			}
		}

		for (int32 Y = Resolution - 1; Y >= 0; --Y)
		{
			for (int32 X = Resolution - 1; X >= 0; --X)
			{
				const int32 I = Y * Resolution + X;
				if (X + 1 < Resolution) Relax(I, I + 1, Cardinal);
				if (Y + 1 < Resolution)
				{
					Relax(I, I + Resolution, Cardinal);
					if (X + 1 < Resolution) Relax(I, I + Resolution + 1, Diagonal);
					if (X > 0) Relax(I, I + Resolution - 1, Diagonal);
				}
			}
		}
	}

	float ComputeSeaLevelThreshold(const FTerrainHeightField& HeightField, float LandCoverage)
	{
		TArray<float> Sorted = HeightField.Data;
		Sorted.Sort();
		const float Coverage = FMath::Clamp(LandCoverage, 0.05f, 0.95f);
		const int32 ThresholdIndex = FMath::Clamp(
			FMath::FloorToInt((1.0f - Coverage) * static_cast<float>(Sorted.Num() - 1)),
			0,
			Sorted.Num() - 1);
		return Sorted[ThresholdIndex];
	}

	void ClassifyDepth(
		float DepthCm,
		const FTerrainLandmassSettings& Settings,
		float& OutShelf,
		float& OutSlope,
		float& OutBasin)
	{
		const float ShelfDepthCm = FMath::Max(Settings.ShelfDepth, 1.0f);
		const float BasinDepthCm = FMath::Max(Settings.BasinDepth, ShelfDepthCm * 2.0f);
		OutShelf = 1.0f - SmoothStep01(DepthCm / ShelfDepthCm);
		OutBasin = SmoothStep01(
			(DepthCm - ShelfDepthCm * 1.75f)
			/ FMath::Max(BasinDepthCm - ShelfDepthCm * 1.75f, 1.0f));
		OutSlope = FMath::Clamp((1.0f - OutShelf) * (1.0f - OutBasin), 0.0f, 1.0f);
	}
}

void FTerrainLandmass::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainStructuralMaps* Structure,
	int32 Seed,
	const FTerrainLandmassSettings& Settings,
	FTerrainLandmassMaps& OutMaps)
{
	OutMaps = FTerrainLandmassMaps{};
	if (!HeightField.IsValid())
	{
		return;
	}

	const int32 NumCells = HeightField.Data.Num();
	OutMaps.BaseElevationCm.SetNumZeroed(NumCells);
	OutMaps.LandInfluence.Init(1.0f, NumCells);
	OutMaps.LandMask.SetNumZeroed(NumCells);
	OutMaps.OceanMask.SetNumZeroed(NumCells);
	OutMaps.CoastMask.SetNumZeroed(NumCells);
	OutMaps.SignedCoastDistanceCm.SetNumZeroed(NumCells);
	OutMaps.BathymetryDepthCm.SetNumZeroed(NumCells);
	OutMaps.ShelfMask.SetNumZeroed(NumCells);
	OutMaps.ContinentalSlopeMask.SetNumZeroed(NumCells);
	OutMaps.OceanBasinMask.SetNumZeroed(NumCells);
	OutMaps.TrenchMask.SetNumZeroed(NumCells);
	OutMaps.SeamountMask.SetNumZeroed(NumCells);
}

void FTerrainLandmass::RefreshSeaLevelClassification(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainLandmassSettings& Settings,
	FTerrainLandmassMaps& InOutMaps)
{
	if (!HeightField.IsValid() || HeightScale <= UE_SMALL_NUMBER || !InOutMaps.IsValidFor(HeightField))
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);

	if (!InOutMaps.bCompositionApplied)
	{
		const TArray<float> OriginalTerrain = HeightField.Data;
		const float Threshold = ComputeSeaLevelThreshold(HeightField, Settings.LandCoverage);
		InOutMaps.SourceSeaLevelThreshold = Threshold;

		TArray<uint8> GeneratedLand;
		GeneratedLand.SetNumZeroed(NumCells);
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			GeneratedLand[Index] = OriginalTerrain[Index] >= Threshold ? 1 : 0;
		}

		if (Settings.bIsland && !Settings.bArchipelago)
		{
			KeepLargestLandComponent(GeneratedLand, Resolution);
		}

		TArray<float> CoastDistance;
		BuildCoastDistance(GeneratedLand, Resolution, CellSize, CoastDistance);

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const bool bLand = GeneratedLand[Index] != 0;
			const float RelativeHeight = OriginalTerrain[Index] - Threshold;
			const float DistanceCm = CoastDistance[Index];

			if (bLand)
			{
				HeightField.Data[Index] = FMath::Max(RelativeHeight, 0.0f);
			}
			else
			{
				const float NaturalDepthCm = FMath::Max(-RelativeHeight * HeightScale, 0.0f);
				const float GentleOffshoreDepthCm = DistanceCm * 0.018f;
				const float MaxGenericDepthCm = FMath::Max(Settings.BasinDepth, Settings.ShelfDepth * 2.0f);
				const float DepthCm = FMath::Clamp(
					NaturalDepthCm * 0.65f + GentleOffshoreDepthCm,
					FMath::Min(5.0f, MaxGenericDepthCm),
					MaxGenericDepthCm);
				HeightField.Data[Index] = -DepthCm / HeightScale;
			}

			InOutMaps.SignedCoastDistanceCm[Index] = bLand ? DistanceCm : -DistanceCm;
		}

		InOutMaps.bCompositionApplied = true;
	}

	const float CoastBandCm = FMath::Max(HeightScale * 0.02f, 50.0f);
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float HeightCm = HeightField.Data[Index] * HeightScale;
		const bool bLand = HeightCm >= 0.0f;
		const float DepthCm = bLand ? 0.0f : -HeightCm;

		InOutMaps.LandMask[Index] = bLand ? 1.0f : 0.0f;
		InOutMaps.OceanMask[Index] = bLand ? 0.0f : 1.0f;
		InOutMaps.BathymetryDepthCm[Index] = DepthCm;
		InOutMaps.CoastMask[Index] = 1.0f - SmoothStep01(FMath::Abs(HeightCm) / CoastBandCm);

		if (bLand)
		{
			InOutMaps.ShelfMask[Index] = 0.0f;
			InOutMaps.ContinentalSlopeMask[Index] = 0.0f;
			InOutMaps.OceanBasinMask[Index] = 0.0f;
			continue;
		}

		ClassifyDepth(
			DepthCm,
			Settings,
			InOutMaps.ShelfMask[Index],
			InOutMaps.ContinentalSlopeMask[Index],
			InOutMaps.OceanBasinMask[Index]);
	}
}
