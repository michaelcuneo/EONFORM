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

	int32 CountLandCells(const TArray<uint8>& Land)
	{
		int32 Count = 0;
		for (const uint8 Value : Land)
		{
			Count += Value != 0 ? 1 : 0;
		}
		return Count;
	}

	void BoxBlur(const TArray<float>& Input, int32 Resolution, int32 Radius, TArray<float>& Output)
	{
		const int32 NumCells = Input.Num();
		Output.SetNumZeroed(NumCells);
		if (Resolution < 2 || NumCells != Resolution * Resolution || Radius <= 0)
		{
			Output = Input;
			return;
		}

		TArray<float> Horizontal;
		Horizontal.SetNumZeroed(NumCells);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			float Sum = 0.0f;
			for (int32 X = -Radius; X <= Radius; ++X)
			{
				Sum += Input[Y * Resolution + FMath::Clamp(X, 0, Resolution - 1)];
			}

			for (int32 X = 0; X < Resolution; ++X)
			{
				Horizontal[Y * Resolution + X] = Sum / static_cast<float>(Radius * 2 + 1);
				const int32 RemoveX = FMath::Clamp(X - Radius, 0, Resolution - 1);
				const int32 AddX = FMath::Clamp(X + Radius + 1, 0, Resolution - 1);
				Sum += Input[Y * Resolution + AddX] - Input[Y * Resolution + RemoveX];
			}
		}

		for (int32 X = 0; X < Resolution; ++X)
		{
			float Sum = 0.0f;
			for (int32 Y = -Radius; Y <= Radius; ++Y)
			{
				Sum += Horizontal[FMath::Clamp(Y, 0, Resolution - 1) * Resolution + X];
			}

			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				Output[Y * Resolution + X] = Sum / static_cast<float>(Radius * 2 + 1);
				const int32 RemoveY = FMath::Clamp(Y - Radius, 0, Resolution - 1);
				const int32 AddY = FMath::Clamp(Y + Radius + 1, 0, Resolution - 1);
				Sum += Horizontal[AddY * Resolution + X] - Horizontal[RemoveY * Resolution + X];
			}
		}
	}

	void BuildReliefSurface(
		const FTerrainHeightField& HeightField,
		const FTerrainLandmassSettings& Settings,
		TArray<float>& OutReliefSurface)
	{
		const int32 Resolution = HeightField.Resolution;
		const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
		const int32 MaxRadius = FMath::Max(2, Resolution / 10);
		const int32 Radius = FMath::Clamp(
			FMath::RoundToInt((Settings.CoastScale / FMath::Max(CellSize, 1.0f)) * 0.035f),
			2,
			MaxRadius);

		TArray<float> SmoothA;
		TArray<float> SmoothB;
		BoxBlur(HeightField.Data, Resolution, Radius, SmoothA);
		BoxBlur(SmoothA, Resolution, Radius, SmoothB);

		const float DetailBlend = FMath::Lerp(
			0.06f,
			0.24f,
			FMath::Clamp(Settings.CoastIrregularity, 0.0f, 1.0f));

		OutReliefSurface.SetNumUninitialized(HeightField.Data.Num());
		for (int32 Index = 0; Index < HeightField.Data.Num(); ++Index)
		{
			OutReliefSurface[Index] = FMath::Lerp(
				SmoothB[Index],
				HeightField.Data[Index],
				DetailBlend);
		}
	}

	void SelectComponentsAtThreshold(
		const TArray<float>& ReliefSurface,
		int32 Resolution,
		float Threshold,
		bool bReserveExteriorOcean,
		bool bSingleIsland,
		TArray<uint8>& OutLand)
	{
		const int32 NumCells = ReliefSurface.Num();
		OutLand.SetNumZeroed(NumCells);
		if (Resolution < 2 || NumCells != Resolution * Resolution)
		{
			return;
		}

		TArray<uint8> Candidate;
		Candidate.SetNumZeroed(NumCells);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				const bool bDomainBoundary = X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1;
				Candidate[Index] = ReliefSurface[Index] >= Threshold
					&& !(bReserveExteriorOcean && bDomainBoundary)
					? 1
					: 0;
			}
		}

		if (!bReserveExteriorOcean)
		{
			OutLand = MoveTemp(Candidate);
			return;
		}

		TArray<uint8> Visited;
		Visited.SetNumZeroed(NumCells);
		TArray<int32> Queue;
		TArray<int32> Component;
		TArray<int32> LargestComponent;
		Queue.Reserve(FMath::Min(NumCells, 262144));

		for (int32 Start = 0; Start < NumCells; ++Start)
		{
			if (Candidate[Start] == 0 || Visited[Start] != 0)
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
						if (NX <= 0 || NX >= Resolution - 1 || NY <= 0 || NY >= Resolution - 1)
						{
							continue;
						}

						const int32 Neighbor = NY * Resolution + NX;
						if (Candidate[Neighbor] != 0 && Visited[Neighbor] == 0)
						{
							Visited[Neighbor] = 1;
							Queue.Add(Neighbor);
						}
					}
				}
			}

			if (bSingleIsland)
			{
				if (Component.Num() > LargestComponent.Num())
				{
					LargestComponent = Component;
				}
			}
			else
			{
				for (const int32 Index : Component)
				{
					OutLand[Index] = 1;
				}
			}
		}

		if (bSingleIsland)
		{
			for (const int32 Index : LargestComponent)
			{
				OutLand[Index] = 1;
			}
		}
	}

	void BuildTerrainDerivedLandMask(
		const FTerrainHeightField& HeightField,
		const FTerrainLandmassSettings& Settings,
		TArray<uint8>& OutLand,
		TArray<float>& OutReliefSurface,
		float& OutThreshold)
	{
		BuildReliefSurface(HeightField, Settings, OutReliefSurface);
		if (OutReliefSurface.IsEmpty())
		{
			OutLand.Reset();
			OutThreshold = 0.0f;
			return;
		}

		float MinValue = LargeDistance;
		float MaxValue = -LargeDistance;
		for (const float Value : OutReliefSurface)
		{
			MinValue = FMath::Min(MinValue, Value);
			MaxValue = FMath::Max(MaxValue, Value);
		}

		const bool bReserveExteriorOcean = Settings.bIsland || Settings.bArchipelago;
		const bool bSingleIsland = Settings.bIsland && !Settings.bArchipelago;
		const int32 TargetCells = FMath::RoundToInt(
			FMath::Clamp(Settings.LandCoverage, 0.05f, 0.90f)
			* static_cast<float>(OutReliefSurface.Num()));

		float Low = MinValue;
		float High = MaxValue;
		int32 BestDifference = MAX_int32;
		float BestThreshold = (Low + High) * 0.5f;
		TArray<uint8> BestMask;

		constexpr int32 SearchIterations = 24;
		for (int32 Iteration = 0; Iteration < SearchIterations; ++Iteration)
		{
			const float Threshold = (Low + High) * 0.5f;
			TArray<uint8> Candidate;
			SelectComponentsAtThreshold(
				OutReliefSurface,
				HeightField.Resolution,
				Threshold,
				bReserveExteriorOcean,
				bSingleIsland,
				Candidate);

			const int32 Count = CountLandCells(Candidate);
			if (Count > 0)
			{
				const int32 Difference = FMath::Abs(Count - TargetCells);
				if (Difference < BestDifference)
				{
					BestDifference = Difference;
					BestThreshold = Threshold;
					BestMask = MoveTemp(Candidate);
				}
			}

			// Lower threshold means more cells can become land. With the domain boundary
			// explicitly reserved as ocean, component size is monotonic enough for a
			// bounded binary search and does not need hundreds of full-grid scans.
			if (Count < TargetCells)
			{
				High = Threshold;
			}
			else
			{
				Low = Threshold;
			}
		}

		if (BestMask.IsEmpty())
		{
			SelectComponentsAtThreshold(
				OutReliefSurface,
				HeightField.Resolution,
				BestThreshold,
				bReserveExteriorOcean,
				bSingleIsland,
				BestMask);
		}

		OutThreshold = BestThreshold;
		OutLand = MoveTemp(BestMask);
	}

	void BuildSubcellCoastDistance(
		const TArray<uint8>& Land,
		const TArray<float>& ReliefSurface,
		float Threshold,
		int32 Resolution,
		float CellSize,
		TArray<float>& OutDistance)
	{
		const int32 NumCells = Land.Num();
		OutDistance.Init(LargeDistance, NumCells);
		if (Resolution < 2 || NumCells != Resolution * Resolution || ReliefSurface.Num() != NumCells)
		{
			return;
		}

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				const bool bLand = Land[Index] != 0;
				const float CenterMagnitude = FMath::Max(FMath::Abs(ReliefSurface[Index] - Threshold), UE_SMALL_NUMBER);

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
						if ((Land[Neighbor] != 0) == bLand)
						{
							continue;
						}

						const float NeighborMagnitude = FMath::Max(FMath::Abs(ReliefSurface[Neighbor] - Threshold), UE_SMALL_NUMBER);
						const float EdgeLength = CellSize * ((OX != 0 && OY != 0) ? DiagonalDistance : 1.0f);
						const float Fraction = CenterMagnitude / (CenterMagnitude + NeighborMagnitude);
						OutDistance[Index] = FMath::Min(OutDistance[Index], EdgeLength * FMath::Clamp(Fraction, 0.05f, 0.95f));
					}
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

	(void)Structure;
	(void)Seed;
	(void)Settings;

	const int32 NumCells = HeightField.Data.Num();
	OutMaps.BaseElevationCm.SetNumZeroed(NumCells);
	OutMaps.LandInfluence.Init(1.0f, NumCells);
	OutMaps.TopologyLandMask.SetNumZeroed(NumCells);
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
	const float MinimumSignedHeight = FMath::Max(1.0f / HeightScale, 1.0e-6f);

	if (!InOutMaps.bCompositionApplied)
	{
		const TArray<float> OriginalTerrain = HeightField.Data;
		TArray<uint8> GeneratedLand;
		TArray<float> ReliefSurface;
		float SeaLevelThreshold = 0.0f;
		BuildTerrainDerivedLandMask(
			HeightField,
			Settings,
			GeneratedLand,
			ReliefSurface,
			SeaLevelThreshold);
		InOutMaps.SourceSeaLevelThreshold = SeaLevelThreshold;

		if (GeneratedLand.Num() != NumCells || CountLandCells(GeneratedLand) <= 0)
		{
			return;
		}

		InOutMaps.TopologyLandMask = GeneratedLand;

		TArray<float> CoastDistance;
		BuildSubcellCoastDistance(
			GeneratedLand,
			ReliefSurface,
			SeaLevelThreshold,
			Resolution,
			CellSize,
			CoastDistance);

		const float ShoreDetailFadeWidth = CellSize * 5.0f;
		const float CoastVisualWidth = CellSize * 3.0f;

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const bool bLand = GeneratedLand[Index] != 0;
			const float Distance = CoastDistance[Index];
			const float BroadRelief = ReliefSurface[Index] - SeaLevelThreshold;
			const float Residual = OriginalTerrain[Index] - ReliefSurface[Index];
			const float DetailFade = SmoothStep01(Distance / ShoreDetailFadeWidth);

			// Topology chooses only the sign. Physical relief comes from the terrain-derived
			// broad surface plus the original residual detail. Boundary closure never enters
			// this magnitude calculation, so it cannot stamp circles or squares into relief.
			float ReliefMagnitude = FMath::Abs(BroadRelief)
				+ Residual * FMath::Lerp(0.18f, 1.0f, DetailFade) * (bLand ? 1.0f : -1.0f);
			ReliefMagnitude = FMath::Max(FMath::Abs(ReliefMagnitude), MinimumSignedHeight);

			HeightField.Data[Index] = bLand ? ReliefMagnitude : -ReliefMagnitude;
			InOutMaps.SignedCoastDistanceCm[Index] = bLand ? Distance : -Distance;
			InOutMaps.CoastMask[Index] = 1.0f - SmoothStep01(Distance / CoastVisualWidth);
		}

		InOutMaps.bCompositionApplied = true;
	}
	else if (Settings.bIsland || Settings.bArchipelago)
	{
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (InOutMaps.TopologyLandMask[Index] != 0)
			{
				HeightField.Data[Index] = FMath::Max(HeightField.Data[Index], MinimumSignedHeight);
			}
			else
			{
				HeightField.Data[Index] = FMath::Min(HeightField.Data[Index], -MinimumSignedHeight);
			}
		}
	}

	const float CoastBandCm = FMath::Max(HeightScale * 0.015f, 50.0f);
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const bool bTopologyLand = (Settings.bIsland || Settings.bArchipelago)
			? InOutMaps.TopologyLandMask[Index] != 0
			: HeightField.Data[Index] > 0.0f;
		const float HeightCm = HeightField.Data[Index] * HeightScale;
		const float DepthCm = bTopologyLand ? 0.0f : FMath::Max(-HeightCm, 0.0f);

		InOutMaps.LandMask[Index] = bTopologyLand ? 1.0f : 0.0f;
		InOutMaps.OceanMask[Index] = bTopologyLand ? 0.0f : 1.0f;
		InOutMaps.BathymetryDepthCm[Index] = DepthCm;

		const float TopologyCoast = 1.0f - SmoothStep01(
			FMath::Abs(InOutMaps.SignedCoastDistanceCm[Index]) / FMath::Max(CellSize * 3.0f, 1.0f));
		const float SeaLevelProximity = 1.0f - SmoothStep01(FMath::Abs(HeightCm) / CoastBandCm);
		InOutMaps.CoastMask[Index] = FMath::Max(TopologyCoast, SeaLevelProximity);

		if (bTopologyLand)
		{
			InOutMaps.ShelfMask[Index] = 0.0f;
			InOutMaps.ContinentalSlopeMask[Index] = 0.0f;
			InOutMaps.OceanBasinMask[Index] = 0.0f;
			InOutMaps.TrenchMask[Index] = 0.0f;
			InOutMaps.SeamountMask[Index] = 0.0f;
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
