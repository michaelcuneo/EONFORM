#include "TerrainPhysiography.h"

#include "TerrainDrainage.h"
#include "TerrainStructure.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
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
		const float Divisor = static_cast<float>(Radius * 2 + 1);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			float Sum = 0.0f;
			for (int32 X = -Radius; X <= Radius; ++X)
			{
				Sum += Input[Y * Resolution + FMath::Clamp(X, 0, Resolution - 1)];
			}
			for (int32 X = 0; X < Resolution; ++X)
			{
				Horizontal[Y * Resolution + X] = Sum / Divisor;
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
				Output[Y * Resolution + X] = Sum / Divisor;
				const int32 RemoveY = FMath::Clamp(Y - Radius, 0, Resolution - 1);
				const int32 AddY = FMath::Clamp(Y + Radius + 1, 0, Resolution - 1);
				Sum += Horizontal[AddY * Resolution + X] - Horizontal[RemoveY * Resolution + X];
			}
		}
	}

	void BuildDistanceFromCore(
		const TArray<float>& Core,
		int32 Resolution,
		float CellSize,
		float Threshold,
		TArray<float>& OutDistance)
	{
		const int32 NumCells = Core.Num();
		const float LargeDistance = CellSize * static_cast<float>(Resolution) * 2.0f;
		OutDistance.Init(LargeDistance, NumCells);

		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (Core[Index] >= Threshold)
			{
				OutDistance[Index] = 0.0f;
			}
		}

		const float Cardinal = CellSize;
		const float Diagonal = CellSize * UE_SQRT_2;
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
}

bool FTerrainPhysiography::Apply(
	FTerrainHeightField& HeightField,
	float HeightScale,
	const FTerrainDrainageMaps& Drainage,
	const FTerrainStructuralMaps* Structure,
	const FTerrainPhysiographySettings& Settings,
	FTerrainPhysiographyMaps& OutMaps)
{
	OutMaps = FTerrainPhysiographyMaps{};
	if (!HeightField.IsValid()
		|| HeightScale <= UE_SMALL_NUMBER
		|| !Drainage.IsValidFor(HeightField))
	{
		return false;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const bool bHasStructure = Structure && Structure->IsValidFor(HeightField);

	OutMaps.Upland.SetNumZeroed(NumCells);
	OutMaps.Hillslope.SetNumZeroed(NumCells);
	OutMaps.Lowland.SetNumZeroed(NumCells);
	OutMaps.Valley.SetNumZeroed(NumCells);
	OutMaps.ValleyFloor.SetNumZeroed(NumCells);
	OutMaps.Bench.SetNumZeroed(NumCells);
	OutMaps.Basin.SetNumZeroed(NumCells);

	const TArray<float> SourceHeight = HeightField.Data;
	TArray<float> LandOnly;
	LandOnly.SetNumZeroed(NumCells);
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		LandOnly[Index] = FMath::Max(SourceHeight[Index], 0.0f);
	}

	const int32 RegionalRadius = FMath::Clamp(
		FMath::RoundToInt(FMath::Max(Settings.RegionalScaleCm, CellSize) / CellSize * 0.35f),
		2,
		FMath::Max(2, Resolution / 12));

	TArray<float> RegionalA;
	TArray<float> RegionalSurface;
	BoxBlur(LandOnly, Resolution, RegionalRadius, RegionalA);
	BoxBlur(RegionalA, Resolution, RegionalRadius, RegionalSurface);

	float MaxRegional = UE_SMALL_NUMBER;
	float MaxLogDrainage = UE_SMALL_NUMBER;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		if (SourceHeight[Index] > 0.0f)
		{
			MaxRegional = FMath::Max(MaxRegional, RegionalSurface[Index]);
			MaxLogDrainage = FMath::Max(
				MaxLogDrainage,
				FMath::Loge(1.0f + FMath::Max(Drainage.FlowAccumulation[Index], 0.0f)));
		}
	}

	TArray<float> ValleyCore;
	ValleyCore.SetNumZeroed(NumCells);
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		if (SourceHeight[Index] <= 0.0f || Drainage.ExteriorOceanMask[Index] > 0.5f)
		{
			continue;
		}

		const float Flow01 = FMath::Clamp(
			FMath::Loge(1.0f + FMath::Max(Drainage.FlowAccumulation[Index], 0.0f)) / MaxLogDrainage,
			0.0f,
			1.0f);
		ValleyCore[Index] = SmoothStep01((Flow01 - 0.18f) / 0.62f);
	}

	TArray<float> DistanceToValley;
	BuildDistanceFromCore(ValleyCore, Resolution, CellSize, 0.15f, DistanceToValley);

	const float SafeValleyWidth = FMath::Max(Settings.ValleyWidthCm, CellSize * 1.5f);
	const float SafeProfile = FMath::Max(Settings.ValleyProfile, 0.25f);
	const float ValleyDepthNormalized = FMath::Max(Settings.ValleyDepthCm, 0.0f) / HeightScale;
	const float BenchHeightNormalized = FMath::Max(Settings.BenchHeightCm, 0.0f) / HeightScale;
	const float MinimumLandHeight = FMath::Max(1.0f / HeightScale, 0.0001f);

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float Original = SourceHeight[Index];
		if (Original <= 0.0f)
		{
			continue;
		}

		const float Regional01 = FMath::Clamp(RegionalSurface[Index] / MaxRegional, 0.0f, 1.0f);
		const float Residual = Original - RegionalSurface[Index];
		const float Uplift = bHasStructure ? FMath::Clamp(Structure->Uplift[Index], 0.0f, 1.0f) : 0.0f;
		const float StructuralValley = bHasStructure ? FMath::Clamp(Structure->LongValley[Index], 0.0f, 1.0f) : 0.0f;

		const float Upland = FMath::Clamp(
			SmoothStep01((Regional01 - 0.48f) / 0.38f) * (0.72f + Uplift * 0.28f),
			0.0f,
			1.0f);
		const float Lowland = FMath::Clamp(
			(1.0f - SmoothStep01((Regional01 - 0.16f) / 0.38f)) * (1.0f - Upland * 0.45f),
			0.0f,
			1.0f);
		const float Hillslope = FMath::Clamp(1.0f - FMath::Max(Upland, Lowland), 0.0f, 1.0f);

		const float Flow01 = FMath::Clamp(
			FMath::Loge(1.0f + FMath::Max(Drainage.FlowAccumulation[Index], 0.0f)) / MaxLogDrainage,
			0.0f,
			1.0f);
		const float WidthMultiplier = FMath::Lerp(0.35f, 2.4f, SmoothStep01(Flow01));
		const float LocalValleyWidth = SafeValleyWidth * WidthMultiplier;
		const float CrossValley = 1.0f - SmoothStep01(DistanceToValley[Index] / LocalValleyWidth);
		const float ValleyHierarchy = FMath::Clamp(
			FMath::Max(ValleyCore[Index], CrossValley * SmoothStep01((Flow01 - 0.08f) / 0.55f))
				+ StructuralValley * 0.18f,
			0.0f,
			1.0f);
		const float ValleyFloor = FMath::Pow(ValleyHierarchy, SafeProfile);

		const float BroadLowland = RegionalSurface[Index] * Settings.LowlandBroadScale;
		const float LowlandTarget = FMath::Max(
			MinimumLandHeight,
			BroadLowland + Residual * Settings.LowlandResidualScale);
		float Shaped = FMath::Lerp(
			Original,
			LowlandTarget,
			Lowland * FMath::Clamp(Settings.LowlandStrength, 0.0f, 1.0f));

		// Subtle rolling relief belongs on lowlands after the broad surface has been
		// established. It is derived from the existing residual, not a new noise field.
		Shaped += Residual * Settings.RollingStrength * Lowland * (1.0f - ValleyFloor);

		const float ValleyIncision = ValleyDepthNormalized
			* FMath::Clamp(Settings.ValleyStrength, 0.0f, 1.0f)
			* ValleyFloor
			* FMath::Lerp(0.22f, 1.0f, SmoothStep01(Flow01));
		Shaped -= ValleyIncision;

		const float ShoulderT = FMath::Clamp(DistanceToValley[Index] / FMath::Max(LocalValleyWidth, CellSize), 0.0f, 2.0f);
		const float BenchBand = SmoothStep01((ShoulderT - 0.52f) / 0.18f)
			* (1.0f - SmoothStep01((ShoulderT - 1.15f) / 0.32f));
		const float Bench = FMath::Clamp(
			BenchBand
			* SmoothStep01((Flow01 - 0.28f) / 0.45f)
			* (1.0f - Upland * 0.65f),
			0.0f,
			1.0f);
		if (Bench > UE_SMALL_NUMBER)
		{
			const float BenchBase = FMath::Max(
				RegionalSurface[Index] - BenchHeightNormalized * FMath::Lerp(0.35f, 1.0f, Flow01),
				MinimumLandHeight);
			Shaped = FMath::Lerp(
				Shaped,
				BenchBase + Residual * 0.12f,
				Bench * FMath::Clamp(Settings.BenchStrength, 0.0f, 1.0f));
		}

		const float Basin = FMath::Clamp(Drainage.LakeMask[Index], 0.0f, 1.0f);
		if (Basin > UE_SMALL_NUMBER)
		{
			const float FillNormalized = FMath::Max(Drainage.FillDepthCm[Index], 0.0f) / HeightScale;
			const float BasinTarget = FMath::Max(
				MinimumLandHeight,
				Original + FillNormalized * FMath::Clamp(Settings.BasinFloorStrength, 0.0f, 1.0f));
			Shaped = FMath::Lerp(Shaped, BasinTarget, Basin * 0.45f);
		}

		HeightField.Data[Index] = FMath::Max(Shaped, MinimumLandHeight);
		OutMaps.Upland[Index] = Upland;
		OutMaps.Hillslope[Index] = Hillslope;
		OutMaps.Lowland[Index] = Lowland;
		OutMaps.Valley[Index] = ValleyHierarchy;
		OutMaps.ValleyFloor[Index] = ValleyFloor;
		OutMaps.Bench[Index] = Bench;
		OutMaps.Basin[Index] = Basin;
	}

	return true;
}
