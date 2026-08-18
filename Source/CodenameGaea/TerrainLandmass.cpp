#include "TerrainLandmass.h"

#include "TerrainNoise.h"
#include "TerrainStructure.h"

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
		if (Resolution < 2 || NumCells != Resolution * Resolution)
		{
			return;
		}

		TArray<uint8> Visited;
		Visited.SetNumZeroed(NumCells);
		TArray<int32> Queue;
		TArray<int32> Component;
		TArray<int32> LargestComponent;
		Queue.Reserve(NumCells);
		Component.Reserve(NumCells / 2);

		for (int32 StartIndex = 0; StartIndex < NumCells; ++StartIndex)
		{
			if (Land[StartIndex] == 0 || Visited[StartIndex] != 0)
			{
				continue;
			}

			Queue.Reset();
			Component.Reset();
			Queue.Add(StartIndex);
			Visited[StartIndex] = 1;
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
						const int32 NeighborIndex = NY * Resolution + NX;
						if (Land[NeighborIndex] != 0 && Visited[NeighborIndex] == 0)
						{
							Visited[NeighborIndex] = 1;
							Queue.Add(NeighborIndex);
						}
					}
				}
			}

			if (Component.Num() > LargestComponent.Num())
			{
				LargestComponent = Component;
			}
		}

		Land.Init(0, NumCells);
		for (const int32 Index : LargestComponent)
		{
			Land[Index] = 1;
		}
	}

	bool IsCoastCell(const TArray<uint8>& Land, int32 Resolution, int32 X, int32 Y)
	{
		const int32 Index = Y * Resolution + X;
		const uint8 CellClass = Land[Index];
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
				const int32 Index = Y * Resolution + X;
				if (IsCoastCell(Land, Resolution, X, Y))
				{
					OutDistance[Index] = 0.0f;
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
		auto Relax = [&OutDistance](int32 Index, int32 NeighborIndex, float Cost)
		{
			OutDistance[Index] = FMath::Min(OutDistance[Index], OutDistance[NeighborIndex] + Cost);
		};

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 Index = Y * Resolution + X;
				if (X > 0) Relax(Index, Index - 1, Cardinal);
				if (Y > 0)
				{
					Relax(Index, Index - Resolution, Cardinal);
					if (X > 0) Relax(Index, Index - Resolution - 1, Diagonal);
					if (X + 1 < Resolution) Relax(Index, Index - Resolution + 1, Diagonal);
				}
			}
		}

		for (int32 Y = Resolution - 1; Y >= 0; --Y)
		{
			for (int32 X = Resolution - 1; X >= 0; --X)
			{
				const int32 Index = Y * Resolution + X;
				if (X + 1 < Resolution) Relax(Index, Index + 1, Cardinal);
				if (Y + 1 < Resolution)
				{
					Relax(Index, Index + Resolution, Cardinal);
					if (X + 1 < Resolution) Relax(Index, Index + Resolution + 1, Diagonal);
					if (X > 0) Relax(Index, Index + Resolution - 1, Diagonal);
				}
			}
		}
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

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float HalfWorldSize = HeightField.WorldSize * 0.5f;
	const bool bHasStructure = Structure && Structure->IsValidFor(HeightField);

	OutMaps.BaseElevationCm.SetNumZeroed(NumCells);
	OutMaps.LandInfluence.SetNumZeroed(NumCells);
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

	FTerrainFractalNoiseSettings CoastNoiseSettings{ 1.0f / FMath::Max(Settings.CoastScale, 100.0f), 4, 0.52f, 2.0f };
	FTerrainFractalNoiseSettings CoastDetailSettings{ 2.4f / FMath::Max(Settings.CoastScale, 100.0f), 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings BasinNoiseSettings{ 1.0f / FMath::Max(Settings.CoastScale * 1.7f, 100.0f), 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings SeamountNoiseSettings{ 1.0f / FMath::Max(Settings.SeamountScale, 100.0f), 3, 0.5f, 2.0f };

	const FVector2D CoastOffset = FTerrainNoise::MakeSeedOffset(Seed, 909);
	const FVector2D CoastDetailOffset = FTerrainNoise::MakeSeedOffset(Seed, 912);
	const FVector2D BasinOffset = FTerrainNoise::MakeSeedOffset(Seed, 910);
	const FVector2D SeamountOffset = FTerrainNoise::MakeSeedOffset(Seed, 911);

	const float CoverageBias = FMath::Lerp(-0.34f, 0.34f, FMath::Clamp(Settings.LandCoverage, 0.0f, 1.0f));
	const float Irregularity = FMath::Clamp(Settings.CoastIrregularity, 0.0f, 1.0f);
	const float EdgeBand = FMath::Max(CellSize * 3.0f, HalfWorldSize * FMath::Clamp(Settings.EdgeOceanMargin, 0.0f, 0.35f));

	TArray<uint8> InitialLand;
	InitialLand.SetNumZeroed(NumCells);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const FVector2D P(static_cast<float>(X) * CellSize - HalfWorldSize, static_cast<float>(Y) * CellSize - HalfWorldSize);
			const float CoastNoise = FTerrainNoise::SampleFractal(P, CoastOffset, CoastNoiseSettings);
			const float CoastDetail = FTerrainNoise::SampleFractal(P, CoastDetailOffset, CoastDetailSettings);
			float LandField = CoastNoise * FMath::Lerp(0.72f, 1.0f, Irregularity)
				+ CoastDetail * Irregularity * 0.34f
				+ CoverageBias;

			if (bHasStructure)
			{
				LandField += (*Structure).Uplift[Index] * 0.24f;
				LandField -= (*Structure).LongValley[Index] * 0.06f;
			}

			if (Settings.bIsland || Settings.bArchipelago)
			{
				const float DistanceToEdge = FMath::Max(0.0f, FMath::Min(HalfWorldSize - FMath::Abs(P.X), HalfWorldSize - FMath::Abs(P.Y)));
				const float EdgeOcean = 1.0f - SmoothStep01(DistanceToEdge / FMath::Max(EdgeBand, CellSize));
				LandField -= EdgeOcean * 1.35f;
			}

			if (Settings.bArchipelago)
			{
				const float Fragmentation = FTerrainNoise::SampleFractal(P * 1.35f, CoastDetailOffset + FVector2D(1700.0f, -2300.0f), CoastDetailSettings);
				LandField += Fragmentation * 0.28f - 0.06f;
			}

			InitialLand[Index] = LandField >= 0.0f ? 1 : 0;
		}
	}

	if (Settings.bIsland && !Settings.bArchipelago)
	{
		KeepLargestLandComponent(InitialLand, Resolution);
	}

	TArray<float> CoastDistance;
	BuildCoastDistance(InitialLand, Resolution, CellSize, CoastDistance);

	const float CoastalEmergenceWidth = FMath::Max(CellSize * 2.0f, Settings.InlandRiseWidth);
	const float CoastVisualWidth = FMath::Max(CellSize * 2.0f, Settings.ShelfWidth * 0.08f);
	const float DomainBasinDepthCap = FMath::Max(Settings.ShelfDepth * 2.0f, HeightField.WorldSize * 0.03f);
	const float TargetBasinDepth = FMath::Min(Settings.BasinDepth, DomainBasinDepthCap);
	const float TargetShelfDepth = FMath::Min(Settings.ShelfDepth, TargetBasinDepth * 0.18f);
	const float TargetBasinRelief = FMath::Min(Settings.BasinRelief, TargetBasinDepth * 0.18f);
	const float TargetTrenchDepth = FMath::Min(Settings.TrenchDepth, TargetBasinDepth * 0.35f);
	const float TargetSeamountHeight = FMath::Min(Settings.SeamountHeight, TargetBasinDepth * 0.55f);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const FVector2D P(static_cast<float>(X) * CellSize - HalfWorldSize, static_cast<float>(Y) * CellSize - HalfWorldSize);
			const bool bLand = InitialLand[Index] != 0;
			const float Distance = CoastDistance[Index];
			const float SignedDistance = bLand ? Distance : -Distance;

			OutMaps.SignedCoastDistanceCm[Index] = SignedDistance;
			OutMaps.CoastMask[Index] = 1.0f - SmoothStep01(Distance / CoastVisualWidth);

			if (bLand)
			{
				const float Inland = SmoothStep01(Distance / CoastalEmergenceWidth);
				const float Emergence = FMath::Sqrt(FMath::Clamp(Inland, 0.0f, 1.0f));
				OutMaps.LandInfluence[Index] = 1.0f;
				OutMaps.BaseElevationCm[Index] = Emergence * Settings.CoastalLandRise;
				OutMaps.LandMask[Index] = 1.0f;
				continue;
			}

			OutMaps.OceanMask[Index] = 1.0f;
			const float OffshoreDistance = Distance;
			const float ShelfVariationNoise = FTerrainNoise::SampleFractal(P, CoastDetailOffset, CoastDetailSettings) * 0.5f + 0.5f;
			const float ShelfVariation = FMath::Lerp(0.65f, 1.25f, FMath::Clamp(ShelfVariationNoise, 0.0f, 1.0f));
			const float IslandShelfScale = (Settings.bIsland || Settings.bArchipelago) ? 0.25f : 1.0f;
			const float IslandSlopeScale = (Settings.bIsland || Settings.bArchipelago) ? 0.55f : 1.0f;
			const float LocalShelfWidth = FMath::Max(CellSize * 2.0f, Settings.ShelfWidth * IslandShelfScale * ShelfVariation);
			const float LocalSlopeWidth = FMath::Max(CellSize * 3.0f, Settings.ContinentalSlopeWidth * IslandSlopeScale * FMath::Lerp(0.85f, 1.15f, ShelfVariationNoise));

			const float ShelfT = SmoothStep01(OffshoreDistance / LocalShelfWidth);
			const float SlopeT = SmoothStep01((OffshoreDistance - LocalShelfWidth) / FMath::Max(LocalSlopeWidth, CellSize));
			const float BasinMask = SmoothStep01(
				(OffshoreDistance - LocalShelfWidth - LocalSlopeWidth * 0.85f)
				/ FMath::Max(LocalSlopeWidth * 0.35f, CellSize));

			const float ShallowProfile = FMath::Pow(FMath::Clamp(OffshoreDistance / LocalShelfWidth, 0.0f, 1.0f), 0.82f);
			float Depth = TargetShelfDepth * ShallowProfile;
			Depth = FMath::Lerp(Depth, TargetBasinDepth, SlopeT);

			const float BasinNoise = FTerrainNoise::SampleFractal(P, BasinOffset, BasinNoiseSettings);
			Depth += BasinNoise * TargetBasinRelief * BasinMask;
			Depth = FMath::Clamp(Depth, 0.0f, TargetBasinDepth + TargetBasinRelief);

			float Trench = 0.0f;
			if (bHasStructure)
			{
				const float DeepWaterSupport = FMath::Clamp(SlopeT * 0.35f + BasinMask, 0.0f, 1.0f);
				Trench = (*Structure).FaultWeakness[Index] * DeepWaterSupport;
				Depth += Trench * TargetTrenchDepth;
			}

			const float SeamountNoise = FTerrainNoise::SampleRidged(P, SeamountOffset, SeamountNoiseSettings, 1.8f);
			const float Seamount = FMath::Pow(FMath::Clamp(SeamountNoise, 0.0f, 1.0f), 3.0f) * BasinMask;
			Depth = FMath::Max(0.0f, Depth - Seamount * TargetSeamountHeight);

			OutMaps.BaseElevationCm[Index] = -Depth;
			OutMaps.BathymetryDepthCm[Index] = Depth;
			OutMaps.ShelfMask[Index] = 1.0f - ShelfT;
			OutMaps.ContinentalSlopeMask[Index] = FMath::Clamp(SlopeT * (1.0f - BasinMask), 0.0f, 1.0f);
			OutMaps.OceanBasinMask[Index] = BasinMask;
			OutMaps.TrenchMask[Index] = Trench;
			OutMaps.SeamountMask[Index] = Seamount;
		}
	}
}

void FTerrainLandmass::RefreshSeaLevelClassification(
	const FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainLandmassSettings& Settings,
	FTerrainLandmassMaps& InOutMaps)
{
	if (!HeightField.IsValid() || HeightScale <= UE_SMALL_NUMBER || !InOutMaps.IsValidFor(HeightField))
	{
		return;
	}

	const int32 NumCells = HeightField.Data.Num();
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float HeightCm = HeightField.Data[Index] * HeightScale;
		const bool bLand = HeightCm >= 0.0f;
		InOutMaps.LandMask[Index] = bLand ? 1.0f : 0.0f;
		InOutMaps.OceanMask[Index] = bLand ? 0.0f : 1.0f;
		InOutMaps.BathymetryDepthCm[Index] = bLand ? 0.0f : -HeightCm;
		const float SeaLevelProximity = 1.0f - SmoothStep01(FMath::Abs(HeightCm) / FMath::Max(Settings.ShelfDepth * 0.6f, 1.0f));
		InOutMaps.CoastMask[Index] = FMath::Max(InOutMaps.CoastMask[Index], SeaLevelProximity);
	}
}
