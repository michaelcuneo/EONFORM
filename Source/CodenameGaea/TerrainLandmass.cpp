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

	void KeepLargestLandComponent(TArray<uint8>& Land, int32 Resolution)
	{
		const int32 NumCells = Land.Num();
		if (Resolution < 2 || NumCells != Resolution * Resolution)
		{
			return;
		}

		TArray<uint8> Visited;
		Visited.SetNumZeroed(NumCells);
		TArray<int32> Queue;
		TArray<int32> Component;
		TArray<int32> Largest;
		Queue.Reserve(NumCells);

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
		bool bHasCoast = false;

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				if (IsCoastCell(Land, Resolution, X, Y))
				{
					OutDistance[Y * Resolution + X] = 0.0f;
					bHasCoast = true;
				}
			}
		}

		if (!bHasCoast)
		{
			const float FallbackDistance = CellSize * static_cast<float>(Resolution);
			for (float& Distance : OutDistance)
			{
				Distance = FallbackDistance;
			}
			return;
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
				if (X > 0)
				{
					Relax(I, I - 1, Cardinal);
				}
				if (Y > 0)
				{
					Relax(I, I - Resolution, Cardinal);
					if (X > 0)
					{
						Relax(I, I - Resolution - 1, Diagonal);
					}
					if (X + 1 < Resolution)
					{
						Relax(I, I - Resolution + 1, Diagonal);
					}
				}
			}
		}

		for (int32 Y = Resolution - 1; Y >= 0; --Y)
		{
			for (int32 X = Resolution - 1; X >= 0; --X)
			{
				const int32 I = Y * Resolution + X;
				if (X + 1 < Resolution)
				{
					Relax(I, I + 1, Cardinal);
				}
				if (Y + 1 < Resolution)
				{
					Relax(I, I + Resolution, Cardinal);
					if (X + 1 < Resolution)
					{
						Relax(I, I + Resolution + 1, Diagonal);
					}
					if (X > 0)
					{
						Relax(I, I + Resolution - 1, Diagonal);
					}
				}
			}
		}
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
			float Running = 0.0f;
			for (int32 X = -Radius; X <= Radius; ++X)
			{
				const int32 ClampedX = FMath::Clamp(X, 0, Resolution - 1);
				Running += Input[Y * Resolution + ClampedX];
			}

			for (int32 X = 0; X < Resolution; ++X)
			{
				Horizontal[Y * Resolution + X] = Running / static_cast<float>(Radius * 2 + 1);
				const int32 RemoveX = FMath::Clamp(X - Radius, 0, Resolution - 1);
				const int32 AddX = FMath::Clamp(X + Radius + 1, 0, Resolution - 1);
				Running += Input[Y * Resolution + AddX] - Input[Y * Resolution + RemoveX];
			}
		}

		for (int32 X = 0; X < Resolution; ++X)
		{
			float Running = 0.0f;
			for (int32 Y = -Radius; Y <= Radius; ++Y)
			{
				const int32 ClampedY = FMath::Clamp(Y, 0, Resolution - 1);
				Running += Horizontal[ClampedY * Resolution + X];
			}

			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				Output[Y * Resolution + X] = Running / static_cast<float>(Radius * 2 + 1);
				const int32 RemoveY = FMath::Clamp(Y - Radius, 0, Resolution - 1);
				const int32 AddY = FMath::Clamp(Y + Radius + 1, 0, Resolution - 1);
				Running += Horizontal[AddY * Resolution + X] - Horizontal[RemoveY * Resolution + X];
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
		const int32 MaxRadius = FMath::Max(2, Resolution / 12);
		const int32 Radius = FMath::Clamp(
			FMath::RoundToInt((Settings.CoastScale / FMath::Max(CellSize, 1.0f)) * 0.04f),
			2,
			MaxRadius);

		TArray<float> SmoothA;
		TArray<float> SmoothB;
		BoxBlur(HeightField.Data, Resolution, Radius, SmoothA);
		BoxBlur(SmoothA, Resolution, Radius, SmoothB);

		const float DetailBlend = FMath::Clamp(Settings.CoastIrregularity, 0.0f, 1.0f) * 0.22f;
		OutSuitability.SetNumUninitialized(HeightField.Data.Num());
		for (int32 Index = 0; Index < HeightField.Data.Num(); ++Index)
		{
			OutSuitability[Index] = FMath::Lerp(SmoothB[Index], HeightField.Data[Index], DetailBlend);
		}
	}

	bool IsAllowedLandCell(int32 X, int32 Y, int32 Resolution, int32 EdgeGuardCells)
	{
		return X >= EdgeGuardCells
			&& X < Resolution - EdgeGuardCells
			&& Y >= EdgeGuardCells
			&& Y < Resolution - EdgeGuardCells;
	}

	void BuildMaskAtThreshold(
		const TArray<float>& Suitability,
		int32 Resolution,
		int32 EdgeGuardCells,
		float Threshold,
		bool bKeepLargest,
		TArray<uint8>& OutLand)
	{
		OutLand.SetNumZeroed(Suitability.Num());
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				if (IsAllowedLandCell(X, Y, Resolution, EdgeGuardCells) && Suitability[Index] >= Threshold)
				{
					OutLand[Index] = 1;
				}
			}
		}

		if (bKeepLargest)
		{
			KeepLargestLandComponent(OutLand, Resolution);
		}
	}

	void BuildTerrainDerivedLandMask(
		const FTerrainHeightField& HeightField,
		const FTerrainLandmassSettings& Settings,
		TArray<uint8>& OutLand,
		float& OutThreshold)
	{
		const int32 Resolution = HeightField.Resolution;
		const int32 NumCells = HeightField.Data.Num();
		TArray<float> Suitability;
		BuildTerrainSuitability(HeightField, Settings, Suitability);

		const bool bBoundedByOcean = Settings.bIsland || Settings.bArchipelago;
		const int32 EdgeGuardCells = bBoundedByOcean
			? FMath::Clamp(
				FMath::RoundToInt(FMath::Clamp(Settings.EdgeOceanMargin, 0.0f, 0.35f) * 0.5f * static_cast<float>(Resolution - 1)),
				1,
				FMath::Max(1, Resolution / 4))
			: 0;

		int32 EligibleCells = 0;
		float MinSuitability = LargeDistance;
		float MaxSuitability = -LargeDistance;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				if (!IsAllowedLandCell(X, Y, Resolution, EdgeGuardCells))
				{
					continue;
				}
				const float Value = Suitability[Y * Resolution + X];
				MinSuitability = FMath::Min(MinSuitability, Value);
				MaxSuitability = FMath::Max(MaxSuitability, Value);
				++EligibleCells;
			}
		}

		if (EligibleCells <= 0 || MinSuitability > MaxSuitability)
		{
			OutLand.SetNumZeroed(NumCells);
			OutThreshold = 0.0f;
			return;
		}

		const int32 RequestedTarget = FMath::RoundToInt(
			FMath::Clamp(Settings.LandCoverage, 0.05f, 0.95f) * static_cast<float>(NumCells));
		const int32 TargetCells = FMath::Clamp(RequestedTarget, 1, FMath::Max(1, FMath::FloorToInt(EligibleCells * 0.97f)));
		const bool bKeepLargest = Settings.bIsland && !Settings.bArchipelago;

		float Low = MinSuitability - UE_SMALL_NUMBER;
		float High = MaxSuitability + UE_SMALL_NUMBER;
		int32 BestDifference = MAX_int32;
		float BestThreshold = (Low + High) * 0.5f;
		TArray<uint8> BestMask;

		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const float Threshold = (Low + High) * 0.5f;
			TArray<uint8> Candidate;
			BuildMaskAtThreshold(Suitability, Resolution, EdgeGuardCells, Threshold, bKeepLargest, Candidate);
			const int32 Count = CountLandCells(Candidate);
			const int32 Difference = FMath::Abs(Count - TargetCells);

			if (Difference < BestDifference)
			{
				BestDifference = Difference;
				BestThreshold = Threshold;
				BestMask = Candidate;
			}

			if (Count > TargetCells)
			{
				Low = Threshold;
			}
			else
			{
				High = Threshold;
			}
		}

		OutThreshold = BestThreshold;
		OutLand = MoveTemp(BestMask);
	}

	void FindRangeForMask(
		const TArray<float>& Values,
		const TArray<uint8>& Mask,
		bool bMaskValue,
		float& OutMin,
		float& OutMax)
	{
		OutMin = LargeDistance;
		OutMax = -LargeDistance;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const bool bSelected = Mask[Index] != 0;
			if (bSelected != bMaskValue)
			{
				continue;
			}
			OutMin = FMath::Min(OutMin, Values[Index]);
			OutMax = FMath::Max(OutMax, Values[Index]);
		}

		if (OutMin > OutMax)
		{
			OutMin = 0.0f;
			OutMax = 1.0f;
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
		float SuitabilityThreshold = 0.0f;
		BuildTerrainDerivedLandMask(HeightField, Settings, GeneratedLand, SuitabilityThreshold);
		InOutMaps.SourceSeaLevelThreshold = SuitabilityThreshold;

		if (CountLandCells(GeneratedLand) <= 0)
		{
			return;
		}

		TArray<float> CoastDistance;
		BuildCoastDistance(GeneratedLand, Resolution, CellSize, CoastDistance);

		float LandMin = 0.0f;
		float LandMax = 1.0f;
		float OceanMin = 0.0f;
		float OceanMax = 1.0f;
		FindRangeForMask(OriginalTerrain, GeneratedLand, true, LandMin, LandMax);
		FindRangeForMask(OriginalTerrain, GeneratedLand, false, OceanMin, OceanMax);
		const float LandSpan = FMath::Max(LandMax - LandMin, UE_SMALL_NUMBER);
		const float OceanSpan = FMath::Max(OceanMax - OceanMin, UE_SMALL_NUMBER);

		float MaxOceanDistance = CellSize;
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (GeneratedLand[Index] == 0)
			{
				MaxOceanDistance = FMath::Max(MaxOceanDistance, CoastDistance[Index] + CellSize * 0.5f);
			}
		}

		const float ShoreTransitionWidth = CellSize * 2.0f;
		const float MinimumSignedSample = 0.003f;
		const float ReliefStrength = FMath::Clamp(
			Settings.BasinRelief / FMath::Max(Settings.BasinDepth, 1.0f),
			0.04f,
			0.22f);

		TArray<float> OceanDepthCandidate;
		OceanDepthCandidate.SetNumZeroed(NumCells);
		float MaxOceanDepthCandidate = MinimumSignedSample;

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (GeneratedLand[Index] != 0)
			{
				continue;
			}

			const float EffectiveDistance = CoastDistance[Index] + CellSize * 0.5f;
			const float Distance01 = FMath::Clamp(EffectiveDistance / MaxOceanDistance, 0.0f, 1.0f);
			const float BaseDepth = FMath::Pow(Distance01, 0.72f);
			const float Raw01 = FMath::Clamp((OriginalTerrain[Index] - OceanMin) / OceanSpan, 0.0f, 1.0f);
			const float RawSigned = Raw01 * 2.0f - 1.0f;
			const float ReliefFade = SmoothStep01(Distance01 / 0.18f);
			const float Candidate = FMath::Max(
				MinimumSignedSample,
				BaseDepth - RawSigned * ReliefStrength * ReliefFade);

			OceanDepthCandidate[Index] = Candidate;
			MaxOceanDepthCandidate = FMath::Max(MaxOceanDepthCandidate, Candidate);
		}

		const float CoastVisualWidth = CellSize * 3.0f;
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const bool bLand = GeneratedLand[Index] != 0;
			const float EffectiveDistance = CoastDistance[Index] + CellSize * 0.5f;
			const float SignedDistance = bLand ? EffectiveDistance : -EffectiveDistance;
			InOutMaps.SignedCoastDistanceCm[Index] = SignedDistance;
			InOutMaps.CoastMask[Index] = 1.0f - SmoothStep01(FMath::Abs(SignedDistance) / CoastVisualWidth);

			if (bLand)
			{
				const float Raw01 = FMath::Clamp((OriginalTerrain[Index] - LandMin) / LandSpan, 0.0f, 1.0f);
				const float NaturalLand = FMath::Lerp(0.015f, 1.0f, Raw01);
				const float ShoreFactor = SmoothStep01(EffectiveDistance / ShoreTransitionWidth);
				HeightField.Data[Index] = FMath::Max(MinimumSignedSample, NaturalLand * ShoreFactor);
			}
			else
			{
				const float Depth01 = FMath::Clamp(
					OceanDepthCandidate[Index] / FMath::Max(MaxOceanDepthCandidate, UE_SMALL_NUMBER),
					MinimumSignedSample,
					1.0f);
				HeightField.Data[Index] = -Depth01;
			}
		}

		InOutMaps.bCompositionApplied = true;
	}

	const float CoastBandCm = FMath::Max(HeightScale * 0.02f, 50.0f);
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
