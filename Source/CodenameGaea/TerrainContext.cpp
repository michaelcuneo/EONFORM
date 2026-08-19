#include "TerrainContext.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	void InitializeField(
		FGaeaScalarField& Field,
		const FGaeaGridDomain& Domain,
		FName Name,
		EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		Field.Initialize(Domain, Descriptor);
	}
}

void FTerrainContext::Analyze(
	const FTerrainHeightField& HeightField,
	float HeightScale,
	const TArray<float>& MountainMask,
	const TArray<float>& FoothillMask,
	const TArray<float>& PlainsMask,
	FTerrainContextMaps& OutContext)
{
	OutContext = FTerrainContextMaps{};

	if (!HeightField.IsValid() || HeightScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const FGaeaGridDomain& Domain = HeightField.GetGaeaDomain();

	InitializeField(OutContext.ElevationField, Domain, TEXT("Elevation"));
	InitializeField(OutContext.SlopeDegreesField, Domain, TEXT("SlopeDegrees"), EGaeaFieldUnit::Degrees);
	InitializeField(OutContext.ConcavityField, Domain, TEXT("Concavity"));
	InitializeField(OutContext.ConvexityField, Domain, TEXT("Convexity"));
	InitializeField(OutContext.MountainField, Domain, TEXT("Mountain"));
	InitializeField(OutContext.FoothillField, Domain, TEXT("Foothill"));
	InitializeField(OutContext.PlainsField, Domain, TEXT("Plains"));

	float MinHeight = TNumericLimits<float>::Max();
	float MaxHeight = TNumericLimits<float>::Lowest();
	for (const float Height : HeightField.Data)
	{
		MinHeight = FMath::Min(MinHeight, Height);
		MaxHeight = FMath::Max(MaxHeight, Height);
	}
	const float HeightRange = FMath::Max(MaxHeight - MinHeight, UE_SMALL_NUMBER);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const float Center = HeightField.Data[Index];
			OutContext.Elevation[Index] = FMath::Clamp((Center - MinHeight) / HeightRange, 0.0f, 1.0f);

			const int32 XL = FMath::Max(0, X - 1);
			const int32 XR = FMath::Min(Resolution - 1, X + 1);
			const int32 YD = FMath::Max(0, Y - 1);
			const int32 YU = FMath::Min(Resolution - 1, Y + 1);

			const float DX = (HeightField.At(XR, Y) - HeightField.At(XL, Y)) * HeightScale
				/ FMath::Max(static_cast<float>(XR - XL) * CellSize, UE_SMALL_NUMBER);
			const float DY = (HeightField.At(X, YU) - HeightField.At(X, YD)) * HeightScale
				/ FMath::Max(static_cast<float>(YU - YD) * CellSize, UE_SMALL_NUMBER);
			const float Gradient = FMath::Sqrt(DX * DX + DY * DY);
			OutContext.SlopeDegrees[Index] = FMath::RadiansToDegrees(FMath::Atan(Gradient));

			float NeighborSum = 0.0f;
			int32 NeighborCount = 0;
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
					NeighborSum += HeightField.At(NX, NY);
					++NeighborCount;
				}
			}

			if (NeighborCount > 0)
			{
				const float Delta = NeighborSum / static_cast<float>(NeighborCount) - Center;
				const float Curvature = FMath::Clamp(Delta * HeightScale / FMath::Max(CellSize, UE_SMALL_NUMBER), -1.0f, 1.0f);
				OutContext.Concavity[Index] = FMath::Max(Curvature, 0.0f);
				OutContext.Convexity[Index] = FMath::Max(-Curvature, 0.0f);
			}

			const float Mountain = MountainMask.Num() == NumCells ? MountainMask[Index] : SmoothStep01((OutContext.Elevation[Index] - 0.58f) / 0.25f);
			const float Foothill = FoothillMask.Num() == NumCells ? FoothillMask[Index] : 0.0f;
			const float Plains = PlainsMask.Num() == NumCells ? PlainsMask[Index] : 1.0f - Mountain;

			OutContext.Mountain[Index] = FMath::Clamp(Mountain, 0.0f, 1.0f);
			OutContext.Foothill[Index] = FMath::Clamp(Foothill, 0.0f, 1.0f);
			OutContext.Plains[Index] = FMath::Clamp(Plains, 0.0f, 1.0f);
		}
	}
}

void FTerrainContext::BuildProcessMasks(
	const FTerrainContextMaps& Context,
	const FTerrainHeightField& HeightField,
	float ThermalTalusAngleDegrees,
	const FTerrainProcessMaskSettings& Settings,
	FTerrainProcessMasks& OutMasks)
{
	OutMasks = FTerrainProcessMasks{};
	if (!Context.IsValidFor(HeightField))
	{
		return;
	}

	const int32 NumCells = HeightField.Data.Num();
	const float ThermalRegionality = FMath::Clamp(Settings.ThermalRegionality, 0.0f, 1.0f);
	const float HydraulicRegionality = FMath::Clamp(Settings.HydraulicRegionality, 0.0f, 1.0f);
	const FGaeaGridDomain& Domain = HeightField.GetGaeaDomain();

	InitializeField(OutMasks.ThermalField, Domain, TEXT("Thermal"));
	InitializeField(OutMasks.RainfallField, Domain, TEXT("Rainfall"));
	InitializeField(OutMasks.HydraulicErosionField, Domain, TEXT("HydraulicErosion"));
	InitializeField(OutMasks.DepositionField, Domain, TEXT("Deposition"));
	InitializeField(OutMasks.EvaporationField, Domain, TEXT("Evaporation"));

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float Slope = Context.SlopeDegrees[Index];
		const float Elevation = Context.Elevation[Index];
		const float Mountain = Context.Mountain[Index];
		const float Foothill = Context.Foothill[Index];
		const float Plains = Context.Plains[Index];
		const float Concavity = Context.Concavity[Index];
		const float Convexity = Context.Convexity[Index];

		const float AboveTalus = SmoothStep01((Slope - ThermalTalusAngleDegrees + 4.0f) / 12.0f);
		const float Highland = FMath::Clamp(Mountain + Foothill * 0.65f, 0.0f, 1.0f);
		const float ThermalNatural = AboveTalus * FMath::Clamp(Highland + Convexity * 0.35f, 0.0f, 1.0f);
		OutMasks.Thermal[Index] = FMath::Lerp(1.0f, ThermalNatural, ThermalRegionality);

		const float Orographic = FMath::Clamp(Elevation * Settings.RainfallHighlandBias + Highland * 0.55f + Foothill * 0.15f, 0.0f, 1.0f);
		const float RainNatural = FMath::Clamp(0.18f + Orographic, 0.0f, 1.0f);
		OutMasks.Rainfall[Index] = FMath::Lerp(1.0f, RainNatural, HydraulicRegionality);

		const float SlopeErosion = SmoothStep01((Slope - 4.0f) / 26.0f);
		const float ErosionNatural = FMath::Clamp(SlopeErosion * (0.35f + Highland * 0.65f) * (1.0f - Concavity * 0.45f), 0.0f, 1.0f);
		OutMasks.HydraulicErosion[Index] = FMath::Lerp(1.0f, ErosionNatural, HydraulicRegionality);

		const float LowSlope = 1.0f - SmoothStep01(Slope / 12.0f);
		const float DepositionNatural = FMath::Clamp(LowSlope * (0.35f + Concavity * 0.65f) * (0.45f + Plains * 0.55f), 0.0f, 1.0f);
		OutMasks.Deposition[Index] = FMath::Lerp(1.0f, DepositionNatural, HydraulicRegionality);

		const float Lowland = 1.0f - Elevation;
		const float EvaporationNatural = FMath::Clamp(0.25f + Lowland * Settings.EvaporationLowlandBias + Plains * 0.25f - Concavity * 0.25f, 0.0f, 1.0f);
		OutMasks.Evaporation[Index] = FMath::Lerp(1.0f, EvaporationNatural, HydraulicRegionality);
	}
}
