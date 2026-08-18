#include "TerrainLandmass.h"

#include "TerrainNoise.h"
#include "TerrainStructure.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
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
	FTerrainFractalNoiseSettings BasinNoiseSettings{ 1.0f / FMath::Max(Settings.CoastScale * 1.7f, 100.0f), 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings SeamountNoiseSettings{ 1.0f / FMath::Max(Settings.SeamountScale, 100.0f), 3, 0.5f, 2.0f };
	const FVector2D CoastOffset = FTerrainNoise::MakeSeedOffset(Seed, 909);
	const FVector2D BasinOffset = FTerrainNoise::MakeSeedOffset(Seed, 910);
	const FVector2D SeamountOffset = FTerrainNoise::MakeSeedOffset(Seed, 911);

	const float CoverageBias = FMath::Lerp(-0.35f, 0.35f, FMath::Clamp(Settings.LandCoverage, 0.0f, 1.0f));
	const float Margin = FMath::Clamp(Settings.EdgeOceanMargin, 0.0f, 0.45f);
	const float SafeHalf = FMath::Max(HalfWorldSize, 1.0f);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const FVector2D P(static_cast<float>(X) * CellSize - HalfWorldSize, static_cast<float>(Y) * CellSize - HalfWorldSize);
			const float CoastNoise = FTerrainNoise::SampleFractal(P, CoastOffset, CoastNoiseSettings);
			const float BasinNoise = FTerrainNoise::SampleFractal(P, BasinOffset, BasinNoiseSettings);
			const float SeamountNoise = FTerrainNoise::SampleRidged(P, SeamountOffset, SeamountNoiseSettings, 1.8f);

			float LandField = CoastNoise * Settings.CoastIrregularity + CoverageBias;
			if (bHasStructure)
			{
				LandField += (*Structure).Uplift[Index] * 0.28f;
				LandField -= (*Structure).LongValley[Index] * 0.08f;
			}

			if (Settings.bIsland || Settings.bArchipelago)
			{
				const float NX = P.X / SafeHalf;
				const float NY = P.Y / SafeHalf;
				const float Radius = FMath::Sqrt(NX * NX + NY * NY);
				const float Edge = SmoothStep01((Radius - (1.0f - Margin - 0.22f)) / 0.28f);
				LandField -= Edge * (Settings.bArchipelago ? 0.8f : 1.25f);

				if (Settings.bArchipelago)
				{
					const float Cluster = FTerrainNoise::SampleFractal(P * 0.78f, CoastOffset + FVector2D(1700.0f, -2300.0f), CoastNoiseSettings);
					LandField += Cluster * 0.32f;
				}
			}
			else
			{
				const float NX = FMath::Abs(P.X) / SafeHalf;
				const float NY = FMath::Abs(P.Y) / SafeHalf;
				const float EdgeProximity = FMath::Max(NX, NY);
				LandField += (0.55f - EdgeProximity) * 0.18f;
			}

			const float SignedDistanceProxy = LandField * Settings.InlandRiseWidth;
			OutMaps.SignedCoastDistanceCm[Index] = SignedDistanceProxy;

			if (LandField >= 0.0f)
			{
				const float Inland = SmoothStep01(FMath::Max(SignedDistanceProxy, 0.0f) / FMath::Max(Settings.InlandRiseWidth, 1.0f));
				OutMaps.LandInfluence[Index] = Inland;
				OutMaps.BaseElevationCm[Index] = Inland * Settings.CoastalLandRise;
				OutMaps.LandMask[Index] = 1.0f;
				OutMaps.CoastMask[Index] = 1.0f - SmoothStep01(FMath::Abs(SignedDistanceProxy) / FMath::Max(Settings.ShelfWidth * 0.4f, 1.0f));
				continue;
			}

			OutMaps.LandInfluence[Index] = 0.0f;
			OutMaps.OceanMask[Index] = 1.0f;
			const float OffshoreDistance = -SignedDistanceProxy;
			const float ShelfT = SmoothStep01(OffshoreDistance / FMath::Max(Settings.ShelfWidth, 1.0f));
			const float SlopeT = SmoothStep01((OffshoreDistance - Settings.ShelfWidth) / FMath::Max(Settings.ContinentalSlopeWidth, 1.0f));
			const float ShelfDepthLocal = FMath::Lerp(0.0f, Settings.ShelfDepth, ShelfT);
			const float DeepDepth = FMath::Lerp(Settings.ShelfDepth, Settings.BasinDepth, SlopeT);
			float Depth = OffshoreDistance <= Settings.ShelfWidth ? ShelfDepthLocal : DeepDepth;

			const float BasinMask = SmoothStep01((OffshoreDistance - Settings.ShelfWidth - Settings.ContinentalSlopeWidth * 0.5f) / FMath::Max(Settings.ContinentalSlopeWidth, 1.0f));
			const float BasinReliefLocal = BasinNoise * Settings.BasinRelief * BasinMask;
			Depth = FMath::Max(0.0f, Depth - BasinReliefLocal);

			float Trench = 0.0f;
			if (bHasStructure)
			{
				Trench = (*Structure).FaultWeakness[Index] * BasinMask;
				Depth += Trench * Settings.TrenchDepth;
			}

			const float Seamount = FMath::Pow(FMath::Clamp(SeamountNoise, 0.0f, 1.0f), 3.0f) * BasinMask;
			Depth = FMath::Max(0.0f, Depth - Seamount * Settings.SeamountHeight);

			OutMaps.BaseElevationCm[Index] = -Depth;
			OutMaps.BathymetryDepthCm[Index] = Depth;
			OutMaps.ShelfMask[Index] = 1.0f - ShelfT;
			OutMaps.ContinentalSlopeMask[Index] = FMath::Clamp(SlopeT * (1.0f - BasinMask * 0.55f), 0.0f, 1.0f);
			OutMaps.OceanBasinMask[Index] = BasinMask;
			OutMaps.TrenchMask[Index] = Trench;
			OutMaps.SeamountMask[Index] = Seamount;
			OutMaps.CoastMask[Index] = 1.0f - SmoothStep01(FMath::Abs(SignedDistanceProxy) / FMath::Max(Settings.ShelfWidth * 0.4f, 1.0f));
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
		InOutMaps.CoastMask[Index] = 1.0f - SmoothStep01(FMath::Abs(HeightCm) / FMath::Max(Settings.ShelfDepth * 0.6f, 1.0f));
	}
}
