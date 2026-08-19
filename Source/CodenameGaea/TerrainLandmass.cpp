#include "TerrainLandmass.h"

DEFINE_LOG_CATEGORY_STATIC(LogTerrainLandmass, Log, All);

namespace
{
	constexpr float DiagonalDistance = 1.41421356237f;
	constexpr float LargeDistance = 1.0e30f;
	constexpr float GrowthCompactnessBias = 0.28f;
	constexpr float SeedDistanceBias = 0.55f;
	constexpr float ExteriorOceanMarginFraction = 0.06f;

	struct FIslandFrontierNode
	{
		int32 Index = INDEX_NONE;
		float Score = -LargeDistance;
	};

	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	bool HasHigherPriority(const FIslandFrontierNode& A, const FIslandFrontierNode& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score, 1.0e-7f))
		{
			return A.Score > B.Score;
		}
		return A.Index < B.Index;
	}

	void HeapPush(TArray<FIslandFrontierNode>& Heap, const FIslandFrontierNode& Node)
	{
		int32 Child = Heap.Add(Node);
		while (Child > 0)
		{
			const int32 Parent = (Child - 1) / 2;
			if (HasHigherPriority(Heap[Parent], Heap[Child]))
			{
				break;
			}
			Swap(Heap[Parent], Heap[Child]);
			Child = Parent;
		}
	}

	bool HeapPop(TArray<FIslandFrontierNode>& Heap, FIslandFrontierNode& OutNode)
	{
		if (Heap.IsEmpty())
		{
			return false;
		}

		OutNode = Heap[0];
		const FIslandFrontierNode Last = Heap.Pop(EAllowShrinking::No);
		if (Heap.IsEmpty())
		{
			return true;
		}

		Heap[0] = Last;
		int32 Parent = 0;
		while (true)
		{
			const int32 Left = Parent * 2 + 1;
			if (Left >= Heap.Num())
			{
				break;
			}

			const int32 Right = Left + 1;
			int32 BestChild = Left;
			if (Right < Heap.Num() && HasHigherPriority(Heap[Right], Heap[Left]))
			{
				BestChild = Right;
			}

			if (HasHigherPriority(Heap[Parent], Heap[BestChild]))
			{
				break;
			}

			Swap(Heap[Parent], Heap[BestChild]);
			Parent = BestChild;
		}

		return true;
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

	int32 CountLandNeighbors(const TArray<uint8>& Land, int32 Resolution, int32 X, int32 Y)
	{
		int32 Count = 0;
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

				Count += Land[NY * Resolution + NX] != 0 ? 1 : 0;
			}
		}
		return Count;
	}

	float ComputeCoastDatum(
		const TArray<uint8>& Land,
		const TArray<float>& ReliefSurface,
		int32 Resolution)
	{
		TArray<float> Crossings;
		Crossings.Reserve(FMath::Max(Resolution * 8, 64));

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				if (X + 1 < Resolution)
				{
					const int32 Right = Index + 1;
					if ((Land[Index] != 0) != (Land[Right] != 0))
					{
						Crossings.Add((ReliefSurface[Index] + ReliefSurface[Right]) * 0.5f);
					}
				}
				if (Y + 1 < Resolution)
				{
					const int32 Down = Index + Resolution;
					if ((Land[Index] != 0) != (Land[Down] != 0))
					{
						Crossings.Add((ReliefSurface[Index] + ReliefSurface[Down]) * 0.5f);
					}
				}
			}
		}

		if (Crossings.IsEmpty())
		{
			return 0.0f;
		}

		Crossings.Sort();
		return Crossings[Crossings.Num() / 2];
	}

	void GrowSingleIsland(
		const TArray<float>& ReliefSurface,
		int32 Resolution,
		float LandCoverage,
		TArray<uint8>& OutLand,
		float& OutCoastDatum)
	{
		const int32 NumCells = ReliefSurface.Num();
		OutLand.SetNumZeroed(NumCells);
		OutCoastDatum = 0.0f;
		if (Resolution < 3 || NumCells != Resolution * Resolution)
		{
			return;
		}

		// Island mode has an explicit exterior-ocean mask. Land growth is never
		// allowed inside this outer ring, so a valid island can never touch the finite
		// raster boundary. The mask is deliberately only a safety domain; the actual
		// coastline is still chosen by terrain-weighted connected growth well inside it.
		const int32 OceanMarginCells = FMath::Clamp(
			FMath::RoundToInt(static_cast<float>(Resolution - 1) * ExteriorOceanMarginFraction),
			3,
			FMath::Max(3, Resolution / 8));
		const int32 InteriorMin = OceanMarginCells;
		const int32 InteriorMax = Resolution - 1 - OceanMarginCells;
		if (InteriorMax <= InteriorMin)
		{
			return;
		}

		float MinRelief = LargeDistance;
		float MaxRelief = -LargeDistance;
		for (int32 Y = InteriorMin; Y <= InteriorMax; ++Y)
		{
			for (int32 X = InteriorMin; X <= InteriorMax; ++X)
			{
				const float Value = ReliefSurface[Y * Resolution + X];
				MinRelief = FMath::Min(MinRelief, Value);
				MaxRelief = FMath::Max(MaxRelief, Value);
			}
		}
		const float ReliefSpan = FMath::Max(MaxRelief - MinRelief, 0.001f);

		const int32 SeedMargin = FMath::Max(
			OceanMarginCells + 2,
			FMath::Clamp(
				FMath::RoundToInt(static_cast<float>(Resolution - 1) * 0.14f),
				2,
				FMath::Max(2, Resolution / 4)));
		int32 SeedIndex = INDEX_NONE;
		float SeedRelief = -LargeDistance;

		for (int32 Y = SeedMargin; Y < Resolution - SeedMargin; ++Y)
		{
			for (int32 X = SeedMargin; X < Resolution - SeedMargin; ++X)
			{
				const int32 Index = Y * Resolution + X;
				if (ReliefSurface[Index] > SeedRelief)
				{
					SeedRelief = ReliefSurface[Index];
					SeedIndex = Index;
				}
			}
		}

		if (SeedIndex == INDEX_NONE)
		{
			return;
		}

		const int32 SeedX = SeedIndex % Resolution;
		const int32 SeedY = SeedIndex / Resolution;
		const int32 InteriorWidth = InteriorMax - InteriorMin + 1;
		const int32 MaxLandCells = InteriorWidth * InteriorWidth;
		const int32 TargetCells = FMath::Clamp(
			FMath::RoundToInt(
				FMath::Clamp(LandCoverage, 0.05f, 0.90f)
				* static_cast<float>(NumCells)),
			1,
			MaxLandCells);

		TArray<float> BestScore;
		BestScore.Init(-LargeDistance, NumCells);
		TArray<uint8> Finalized;
		Finalized.SetNumZeroed(NumCells);
		TArray<FIslandFrontierNode> Heap;
		Heap.Reserve(FMath::Min(TargetCells / 4 + 1024, 262144));

		auto TerrainScore = [&](int32 Index)
		{
			return FMath::Clamp((ReliefSurface[Index] - MinRelief) / ReliefSpan, 0.0f, 1.0f);
		};

		auto DistancePenalty = [&](int32 X, int32 Y)
		{
			const float DX = static_cast<float>(X - SeedX) / static_cast<float>(Resolution - 1);
			const float DY = static_cast<float>(Y - SeedY) / static_cast<float>(Resolution - 1);
			const float Distance01 = FMath::Clamp(FMath::Sqrt(DX * DX + DY * DY) * DiagonalDistance, 0.0f, 1.0f);
			return Distance01 * SeedDistanceBias;
		};

		BestScore[SeedIndex] = TerrainScore(SeedIndex);
		HeapPush(Heap, { SeedIndex, BestScore[SeedIndex] });

		int32 LandCount = 0;
		FIslandFrontierNode Node;
		while (LandCount < TargetCells && HeapPop(Heap, Node))
		{
			if (Node.Index == INDEX_NONE || Finalized[Node.Index] != 0)
			{
				continue;
			}
			if (Node.Score + 1.0e-6f < BestScore[Node.Index])
			{
				continue;
			}

			Finalized[Node.Index] = 1;
			OutLand[Node.Index] = 1;
			++LandCount;

			const int32 X = Node.Index % Resolution;
			const int32 Y = Node.Index / Resolution;
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
					if (NX < InteriorMin || NX > InteriorMax || NY < InteriorMin || NY > InteriorMax)
					{
						continue;
					}

					const int32 Neighbor = NY * Resolution + NX;
					if (Finalized[Neighbor] != 0)
					{
						continue;
					}

					const float Support = static_cast<float>(CountLandNeighbors(OutLand, Resolution, NX, NY)) / 8.0f;
					const float CandidateScore = TerrainScore(Neighbor)
						+ Support * GrowthCompactnessBias
						- DistancePenalty(NX, NY);
					if (CandidateScore > BestScore[Neighbor] + 1.0e-6f)
					{
						BestScore[Neighbor] = CandidateScore;
						HeapPush(Heap, { Neighbor, CandidateScore });
					}
				}
			}
		}

		UE_LOG(
			LogTerrainLandmass,
			Display,
			TEXT("Island exterior-ocean mask: %d cells per edge (%.1f%% of resolution), selected land=%d/%d"),
			OceanMarginCells,
			100.0f * static_cast<float>(OceanMarginCells) / static_cast<float>(Resolution - 1),
			LandCount,
			TargetCells);

		OutCoastDatum = ComputeCoastDatum(OutLand, ReliefSurface, Resolution);
	}

	void BuildThresholdLandMask(
		const TArray<float>& ReliefSurface,
		int32 Resolution,
		float LandCoverage,
		bool bReserveExteriorOcean,
		TArray<uint8>& OutLand,
		float& OutThreshold)
	{
		const int32 NumCells = ReliefSurface.Num();
		OutLand.SetNumZeroed(NumCells);
		OutThreshold = 0.0f;
		if (Resolution < 2 || NumCells != Resolution * Resolution)
		{
			return;
		}

		float MinValue = LargeDistance;
		float MaxValue = -LargeDistance;
		for (const float Value : ReliefSurface)
		{
			MinValue = FMath::Min(MinValue, Value);
			MaxValue = FMath::Max(MaxValue, Value);
		}

		const int32 TargetCells = FMath::RoundToInt(
			FMath::Clamp(LandCoverage, 0.05f, 0.90f) * static_cast<float>(NumCells));
		float Low = MinValue;
		float High = MaxValue;

		constexpr int32 SearchIterations = 24;
		for (int32 Iteration = 0; Iteration < SearchIterations; ++Iteration)
		{
			const float Threshold = (Low + High) * 0.5f;
			int32 Count = 0;
			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				for (int32 X = 0; X < Resolution; ++X)
				{
					const bool bBoundary = X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1;
					if (ReliefSurface[Y * Resolution + X] >= Threshold && !(bReserveExteriorOcean && bBoundary))
					{
						++Count;
					}
				}
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

		OutThreshold = (Low + High) * 0.5f;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				const bool bBoundary = X == 0 || X == Resolution - 1 || Y == 0 || Y == Resolution - 1;
				OutLand[Index] = ReliefSurface[Index] >= OutThreshold && !(bReserveExteriorOcean && bBoundary) ? 1 : 0;
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

		if (Settings.bIsland && !Settings.bArchipelago)
		{
			GrowSingleIsland(
				OutReliefSurface,
				HeightField.Resolution,
				Settings.LandCoverage,
				OutLand,
				OutThreshold);
			return;
		}

		BuildThresholdLandMask(
			OutReliefSurface,
			HeightField.Resolution,
			Settings.LandCoverage,
			Settings.bArchipelago,
			OutLand,
			OutThreshold);
	}

	void BuildSubcellCoastDistance(
		const TArray<uint8>& Land,
		int32 Resolution,
		float CellSize,
		TArray<float>& OutDistance)
	{
		const int32 NumCells = Land.Num();
		OutDistance.Init(LargeDistance, NumCells);
		if (Resolution < 2 || NumCells != Resolution * Resolution)
		{
			return;
		}

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				const bool bLand = Land[Index] != 0;

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

						const float EdgeLength = CellSize * ((OX != 0 && OY != 0) ? DiagonalDistance : 1.0f);
						OutDistance[Index] = FMath::Min(OutDistance[Index], EdgeLength * 0.5f);
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

	float BuildBathymetryDepthCm(
		float DistanceFromCoastCm,
		float BroadSignedCm,
		float ResidualCm,
		float CellSize,
		const FTerrainLandmassSettings& Settings)
	{
		const float ShelfWidthCm = FMath::Max(Settings.ShelfWidth, CellSize * 2.0f);
		const float SlopeWidthCm = FMath::Max(Settings.ContinentalSlopeWidth, CellSize * 3.0f);
		const float ShelfDepthCm = FMath::Max(Settings.ShelfDepth, 1.0f);
		const float BasinDepthCm = FMath::Max(Settings.BasinDepth, ShelfDepthCm + 1.0f);
		const float Distance = FMath::Max(DistanceFromCoastCm, 0.0f);

		float ProfileDepthCm = 0.0f;
		if (Distance <= ShelfWidthCm)
		{
			const float ShelfT = SmoothStep01(Distance / ShelfWidthCm);
			ProfileDepthCm = ShelfDepthCm * ShelfT;
		}
		else
		{
			const float SlopeT = SmoothStep01((Distance - ShelfWidthCm) / SlopeWidthCm);
			ProfileDepthCm = FMath::Lerp(ShelfDepthCm, BasinDepthCm, SlopeT);
		}

		const float OffshoreT = SmoothStep01(
			Distance / FMath::Max(ShelfWidthCm * 0.65f, CellSize * 4.0f));
		const float DeepWaterT = SmoothStep01(
			(Distance - ShelfWidthCm * 0.35f)
			/ FMath::Max(ShelfWidthCm + SlopeWidthCm, CellSize));

		const float NaturalDeepeningCm = FMath::Min(
			FMath::Max(-BroadSignedCm, 0.0f) * 0.45f,
			FMath::Max(Settings.TrenchDepth, 0.0f));
		const float NaturalShoalingCm = FMath::Min(
			FMath::Max(BroadSignedCm, 0.0f) * 0.35f,
			FMath::Max(Settings.SeamountHeight, 0.0f));
		const float ResidualReliefCm = FMath::Clamp(
			-ResidualCm,
			-FMath::Max(Settings.BasinRelief, 0.0f),
			FMath::Max(Settings.BasinRelief, 0.0f));

		float DepthCm = ProfileDepthCm;
		DepthCm += NaturalDeepeningCm * OffshoreT;
		DepthCm -= NaturalShoalingCm * DeepWaterT;
		DepthCm += ResidualReliefCm * OffshoreT * 0.65f;

		const float MinimumDepthCm = FMath::Max(1.0f, ShelfDepthCm * 0.0025f);
		return FMath::Max(DepthCm, MinimumDepthCm);
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

		if (GeneratedLand.Num() != NumCells)
		{
			return;
		}

		int32 LandCount = 0;
		for (const uint8 Value : GeneratedLand)
		{
			LandCount += Value != 0 ? 1 : 0;
		}
		if (LandCount <= 0)
		{
			return;
		}

		const int32 OceanCount = NumCells - LandCount;
		const float ActualCoverage = static_cast<float>(LandCount) / static_cast<float>(NumCells);
		UE_LOG(
			LogTerrainLandmass,
			Display,
			TEXT("Signed DEM topology: land=%d (%.2f%%), ocean=%d (%.2f%%), requested land=%.2f%%"),
			LandCount,
			ActualCoverage * 100.0f,
			OceanCount,
			(1.0f - ActualCoverage) * 100.0f,
			Settings.LandCoverage * 100.0f);

		if ((Settings.bIsland || Settings.bArchipelago) && OceanCount <= 0)
		{
			UE_LOG(LogTerrainLandmass, Error, TEXT("Island topology contains no ocean cells; refusing signed-DEM composition."));
			return;
		}

		if (Settings.bIsland && !Settings.bArchipelago
			&& FMath::Abs(ActualCoverage - FMath::Clamp(Settings.LandCoverage, 0.05f, 0.90f)) > 0.02f)
		{
			UE_LOG(
				LogTerrainLandmass,
				Error,
				TEXT("Island topology coverage drifted from requested coverage by more than 2%%. This indicates a topology regression."));
		}

		InOutMaps.TopologyLandMask = GeneratedLand;

		TArray<float> CoastDistance;
		BuildSubcellCoastDistance(
			GeneratedLand,
			Resolution,
			CellSize,
			CoastDistance);

		const float ShoreDetailFadeWidth = CellSize * 5.0f;
		const float CoastVisualWidth = CellSize * 3.0f;

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			const bool bLand = GeneratedLand[Index] != 0;
			const float Distance = CoastDistance[Index];
			const float BroadSigned = ReliefSurface[Index] - SeaLevelThreshold;
			const float Residual = OriginalTerrain[Index] - ReliefSurface[Index];
			const float DetailFade = SmoothStep01(Distance / ShoreDetailFadeWidth);

			if (bLand)
			{
				float SignedHeight = BroadSigned
					+ Residual * FMath::Lerp(0.10f, 1.0f, DetailFade);
				HeightField.Data[Index] = FMath::Max(SignedHeight, MinimumSignedHeight);
			}
			else
			{
				const float DepthCm = BuildBathymetryDepthCm(
					Distance,
					BroadSigned * HeightScale,
					Residual * HeightScale,
					CellSize,
					Settings);
				HeightField.Data[Index] = -DepthCm / HeightScale;
			}

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

		const float BasinDepthCm = FMath::Max(Settings.BasinDepth, Settings.ShelfDepth + 1.0f);
		const float TrenchRangeCm = FMath::Max(Settings.TrenchDepth, 1.0f);
		InOutMaps.TrenchMask[Index] = SmoothStep01(
			(DepthCm - BasinDepthCm) / TrenchRangeCm);

		const float OffshoreT = SmoothStep01(
			(FMath::Abs(InOutMaps.SignedCoastDistanceCm[Index]) - Settings.ShelfWidth)
			/ FMath::Max(Settings.ContinentalSlopeWidth, CellSize));
		const float SeamountReferenceDepth = FMath::Max(BasinDepthCm - Settings.SeamountHeight, Settings.ShelfDepth);
		InOutMaps.SeamountMask[Index] = OffshoreT
			* (1.0f - SmoothStep01(
				(DepthCm - SeamountReferenceDepth)
				/ FMath::Max(Settings.SeamountHeight, 1.0f)));
	}
}
