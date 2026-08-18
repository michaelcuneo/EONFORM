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

	void BuildTerrainSuitability(
		const FTerrainHeightField& HeightField,
		const FTerrainLandmassSettings& Settings,
		TArray<float>& OutSuitability)
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

		const float DetailBlend = FMath::Lerp(0.06f, 0.24f, FMath::Clamp(Settings.CoastIrregularity, 0.0f, 1.0f));
		OutSuitability.SetNumUninitialized(HeightField.Data.Num());

		float BaseMin = LargeDistance;
		float BaseMax = -LargeDistance;
		for (int32 Index = 0; Index < HeightField.Data.Num(); ++Index)
		{
			const float Value = FMath::Lerp(SmoothB[Index], HeightField.Data[Index], DetailBlend);
			OutSuitability[Index] = Value;
			BaseMin = FMath::Min(BaseMin, Value);
			BaseMax = FMath::Max(BaseMax, Value);
		}

		// The island must remain closed inside the finite simulation domain, but the
		// closure mechanism must not become the island shape. Only a thin outer guard
		// band is biased toward ocean. Everywhere else, the coastline is determined
		// exclusively by the terrain-derived suitability field and sea-level threshold.
		if (Settings.bIsland || Settings.bArchipelago)
		{
			const float Span = FMath::Max(BaseMax - BaseMin, 0.001f);
			const int32 GuardCells = FMath::Clamp(
				FMath::RoundToInt(static_cast<float>(Resolution - 1) * 0.025f),
				2,
				FMath::Max(2, Resolution / 16));
			const float GuardStrength = Span * 1.25f;

			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				for (int32 X = 0; X < Resolution; ++X)
				{
					const int32 DistanceToEdge = FMath::Min(
						FMath::Min(X, Resolution - 1 - X),
						FMath::Min(Y, Resolution - 1 - Y));
					if (DistanceToEdge >= GuardCells)
					{
						continue;
					}

					const float EdgeT = 1.0f - static_cast<float>(DistanceToEdge) / static_cast<float>(GuardCells);
					const float EdgeWeight = SmoothStep01(EdgeT);
					OutSuitability[Y * Resolution + X] -= EdgeWeight * GuardStrength;
				}
			}
		}
	}

	void SelectInteriorComponents(
		const TArray<float>& Suitability,
		int32 Resolution,
		float Threshold,
		bool bSingleIsland,
		TArray<uint8>& OutLand)
	{
		const int32 NumCells = Suitability.Num();
		OutLand.SetNumZeroed(NumCells);
		if (Resolution < 2 || NumCells != Resolution * Resolution)
		{
			return;
		}

		TArray<uint8> Candidate;
		Candidate.SetNumZeroed(NumCells);
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			Candidate[Index] = Suitability[Index] >= Threshold ? 1 : 0;
		}

		TArray<uint8> Visited;
		Visited.SetNumZeroed(NumCells);
		TArray<int32> Queue;
		TArray<int32> Component;
		TArray<int32> LargestInterior;
		Queue.Reserve(NumCells);

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
			bool bTouchesBoundary = false;
			int32 Head = 0;

			while (Head < Queue.Num())
			{
				const int32 Index = Queue[Head++];
				Component.Add(Index);
				const int32 X = Index % Resolution;
				const int32 Y = Index / Resolution;
				bTouchesBoundary |= X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1;

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
						if (Candidate[Neighbor] != 0 && Visited[Neighbor] == 0)
						{
							Visited[Neighbor] = 1;
							Queue.Add(Neighbor);
						}
					}
				}
			}

			if (bTouchesBoundary)
			{
				continue;
			}

			if (bSingleIsland)
			{
				if (Component.Num() > LargestInterior.Num())
				{
					LargestInterior = Component;
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
			for (const int32 Index : LargestInterior)
			{
				OutLand[Index] = 1;
			}
		}
	}

	void BuildTerrainDerivedLandMask(
		const FTerrainHeightField& HeightField,
		const FTerrainLandmassSettings& Settings,
		TArray<uint8>& OutLand,
		TArray<float>& OutSuitability,
		float& OutThreshold)
	{
		BuildTerrainSuitability(HeightField, Settings, OutSuitability);
		if (OutSuitability.IsEmpty())
		{
			OutLand.Reset();
			OutThreshold = 0.0f;
			return;
		}

		float MinValue = LargeDistance;
		float MaxValue = -LargeDistance;
		for (const float Value : OutSuitability)
		{
			MinValue = FMath::Min(MinValue, Value);
			MaxValue = FMath::Max(MaxValue, Value);
		}

		const int32 TargetCells = FMath::RoundToInt(
			FMath::Clamp(Settings.LandCoverage, 0.05f, 0.90f) * static_cast<float>(OutSuitability.Num()));
		const bool bSingleIsland = Settings.bIsland && !Settings.bArchipelago;
		const bool bInteriorOnly = Settings.bIsland || Settings.bArchipelago;

		int32 BestDifference = MAX_int32;
		float BestThreshold = (MinValue + MaxValue) * 0.5f;
		TArray<uint8> BestMask;

		constexpr int32 ThresholdSamples = 160;
		for (int32 Sample = 0; Sample < ThresholdSamples; ++Sample)
		{
			const float T = static_cast<float>(Sample) / static_cast<float>(ThresholdSamples - 1);
			const float Threshold = FMath::Lerp(MaxValue, MinValue, T);
			TArray<uint8> Candidate;

			if (bInteriorOnly)
			{
				SelectInteriorComponents(OutSuitability, HeightField.Resolution, Threshold, bSingleIsland, Candidate);
			}
			else
			{
				Candidate.SetNumZeroed(OutSuitability.Num());
				for (int32 Index = 0; Index < OutSuitability.Num(); ++Index)
				{
					Candidate[Index] = OutSuitability[Index] >= Threshold ? 1 : 0;
				}
			}

			const int32 Count = CountLandCells(Candidate);
			if (Count <= 0)
			{
				continue;
			}

			const int32 Difference = FMath::Abs(Count - TargetCells);
			if (Difference < BestDifference)
			{
				BestDifference = Difference;
				BestThreshold = Threshold;
				BestMask = MoveTemp(Candidate);
			}
		}

		OutThreshold = BestThreshold;
		OutLand = MoveTemp(BestMask);
	}

	void BuildSubcellCoastDistance(
		const TArray<uint8>& Land,
		const TArray<float>& Suitability,
		float Threshold,
		int32 Resolution,
		float CellSize,
		TArray<float>& OutDistance)
	{
		const int32 NumCells = Land.Num();
		OutDistance.Init(LargeDistance, NumCells);
		if (Resolution < 2 || NumCells != Resolution * Resolution || Suitability.Num() != NumCells)
		{
			return;
		}

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				const bool bLand = Land[Index] != 0;
				const float CenterMagnitude = FMath::Max(FMath::Abs(Suitability[Index] - Threshold), UE_SMALL_NUMBER);

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

						const float NeighborMagnitude = FMath::Max(FMath::Abs(Suitability[Neighbor] - Threshold), UE_SMALL_NUMBER);
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
		TArray<uint8> GeneratedLand;
		TArray<float> Suitability;
		float SeaLevelThreshold = 0.0f;
		BuildTerrainDerivedLandMask(HeightField, Settings, GeneratedLand, Suitability, SeaLevelThreshold);
		InOutMaps.SourceSeaLevelThreshold = SeaLevelThreshold;

		if (GeneratedLand.Num() != NumCells || CountLandCells(GeneratedLand) <= 0)
		{
			return;
		}

		TArray<float> CoastDistance;
		BuildSubcellCoastDistance(
			GeneratedLand,
			Suitability,
			SeaLevelThreshold,
			Resolution,
			CellSize,
			CoastDistance);

		const float ShoreDetailFadeWidth = CellSize * 5.0f;
		const float MinimumSignedHeight = 0.0001f;
		const float CoastVisualWidth = CellSize * 3.0f;

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const bool bLand = GeneratedLand[Index] != 0;
			const float Distance = CoastDistance[Index];
			const float LowFrequency = Suitability[Index] - SeaLevelThreshold;
			const float Residual = OriginalTerrain[Index] - Suitability[Index];
			const float DetailFade = SmoothStep01(Distance / ShoreDetailFadeWidth);

			// One continuous terrain decomposition: low-frequency relief establishes the
			// coastline, while the original high-frequency terrain returns progressively
			// away from the shore. No land/ocean min-max normalization is performed.
			float SignedHeight = (bLand ? FMath::Abs(LowFrequency) : -FMath::Abs(LowFrequency))
				+ Residual * FMath::Lerp(0.18f, 1.0f, DetailFade);

			// Preserve the selected topology without creating a flat zero shelf. If detail
			// tries to cross the coast locally, retain the low-frequency relief magnitude.
			if (bLand && SignedHeight <= 0.0f)
			{
				SignedHeight = FMath::Max(FMath::Abs(LowFrequency) * 0.35f, MinimumSignedHeight);
			}
			else if (!bLand && SignedHeight >= 0.0f)
			{
				SignedHeight = -FMath::Max(FMath::Abs(LowFrequency) * 0.35f, MinimumSignedHeight);
			}

			HeightField.Data[Index] = SignedHeight;
			InOutMaps.SignedCoastDistanceCm[Index] = bLand ? Distance : -Distance;
			InOutMaps.CoastMask[Index] = 1.0f - SmoothStep01(Distance / CoastVisualWidth);
		}

		InOutMaps.bCompositionApplied = true;
	}

	const float CoastBandCm = FMath::Max(HeightScale * 0.015f, 50.0f);
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float HeightCm = HeightField.Data[Index] * HeightScale;
		const bool bLand = HeightCm > 0.0f;
		const float DepthCm = bLand ? 0.0f : -HeightCm;

		InOutMaps.LandMask[Index] = bLand ? 1.0f : 0.0f;
		InOutMaps.OceanMask[Index] = bLand ? 0.0f : 1.0f;
		InOutMaps.BathymetryDepthCm[Index] = DepthCm;

		const float TopologyCoast = 1.0f - SmoothStep01(
			FMath::Abs(InOutMaps.SignedCoastDistanceCm[Index]) / FMath::Max(CellSize * 3.0f, 1.0f));
		const float SeaLevelProximity = 1.0f - SmoothStep01(FMath::Abs(HeightCm) / CoastBandCm);
		InOutMaps.CoastMask[Index] = FMath::Max(TopologyCoast, SeaLevelProximity);

		if (bLand)
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
