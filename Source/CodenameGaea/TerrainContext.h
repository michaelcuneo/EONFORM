#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainContextMaps
{
	FGaeaScalarField ElevationField;
	FGaeaScalarField SlopeDegreesField;
	FGaeaScalarField ConcavityField;
	FGaeaScalarField ConvexityField;
	FGaeaScalarField MountainField;
	FGaeaScalarField FoothillField;
	FGaeaScalarField PlainsField;

	TArray<float>& Elevation;
	TArray<float>& SlopeDegrees;
	TArray<float>& Concavity;
	TArray<float>& Convexity;
	TArray<float>& Mountain;
	TArray<float>& Foothill;
	TArray<float>& Plains;

	FTerrainContextMaps()
		: Elevation(ElevationField.Values)
		, SlopeDegrees(SlopeDegreesField.Values)
		, Concavity(ConcavityField.Values)
		, Convexity(ConvexityField.Values)
		, Mountain(MountainField.Values)
		, Foothill(FoothillField.Values)
		, Plains(PlainsField.Values)
	{
	}

	FTerrainContextMaps(const FTerrainContextMaps& Other)
		: ElevationField(Other.ElevationField)
		, SlopeDegreesField(Other.SlopeDegreesField)
		, ConcavityField(Other.ConcavityField)
		, ConvexityField(Other.ConvexityField)
		, MountainField(Other.MountainField)
		, FoothillField(Other.FoothillField)
		, PlainsField(Other.PlainsField)
		, Elevation(ElevationField.Values)
		, SlopeDegrees(SlopeDegreesField.Values)
		, Concavity(ConcavityField.Values)
		, Convexity(ConvexityField.Values)
		, Mountain(MountainField.Values)
		, Foothill(FoothillField.Values)
		, Plains(PlainsField.Values)
	{
	}

	FTerrainContextMaps& operator=(const FTerrainContextMaps& Other)
	{
		if (this != &Other)
		{
			ElevationField = Other.ElevationField;
			SlopeDegreesField = Other.SlopeDegreesField;
			ConcavityField = Other.ConcavityField;
			ConvexityField = Other.ConvexityField;
			MountainField = Other.MountainField;
			FoothillField = Other.FoothillField;
			PlainsField = Other.PlainsField;
		}
		return *this;
	}

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const FGaeaGridDomain& Domain = HeightField.GetGaeaDomain();
		return HeightField.IsValid()
			&& ElevationField.IsValid() && ElevationField.Domain == Domain
			&& SlopeDegreesField.IsValid() && SlopeDegreesField.Domain == Domain
			&& ConcavityField.IsValid() && ConcavityField.Domain == Domain
			&& ConvexityField.IsValid() && ConvexityField.Domain == Domain
			&& MountainField.IsValid() && MountainField.Domain == Domain
			&& FoothillField.IsValid() && FoothillField.Domain == Domain
			&& PlainsField.IsValid() && PlainsField.Domain == Domain;
	}
};

struct FTerrainProcessMaskSettings
{
	float ThermalRegionality = 0.85f;
	float HydraulicRegionality = 0.8f;
	float RainfallHighlandBias = 0.65f;
	float EvaporationLowlandBias = 0.55f;
};

struct FTerrainProcessMasks
{
	FGaeaScalarField ThermalField;
	FGaeaScalarField RainfallField;
	FGaeaScalarField HydraulicErosionField;
	FGaeaScalarField DepositionField;
	FGaeaScalarField EvaporationField;

	TArray<float>& Thermal;
	TArray<float>& Rainfall;
	TArray<float>& HydraulicErosion;
	TArray<float>& Deposition;
	TArray<float>& Evaporation;

	FTerrainProcessMasks()
		: Thermal(ThermalField.Values)
		, Rainfall(RainfallField.Values)
		, HydraulicErosion(HydraulicErosionField.Values)
		, Deposition(DepositionField.Values)
		, Evaporation(EvaporationField.Values)
	{
	}

	FTerrainProcessMasks(const FTerrainProcessMasks& Other)
		: ThermalField(Other.ThermalField)
		, RainfallField(Other.RainfallField)
		, HydraulicErosionField(Other.HydraulicErosionField)
		, DepositionField(Other.DepositionField)
		, EvaporationField(Other.EvaporationField)
		, Thermal(ThermalField.Values)
		, Rainfall(RainfallField.Values)
		, HydraulicErosion(HydraulicErosionField.Values)
		, Deposition(DepositionField.Values)
		, Evaporation(EvaporationField.Values)
	{
	}

	FTerrainProcessMasks& operator=(const FTerrainProcessMasks& Other)
	{
		if (this != &Other)
		{
			ThermalField = Other.ThermalField;
			RainfallField = Other.RainfallField;
			HydraulicErosionField = Other.HydraulicErosionField;
			DepositionField = Other.DepositionField;
			EvaporationField = Other.EvaporationField;
		}
		return *this;
	}

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const FGaeaGridDomain& Domain = HeightField.GetGaeaDomain();
		return HeightField.IsValid()
			&& ThermalField.IsValid() && ThermalField.Domain == Domain
			&& RainfallField.IsValid() && RainfallField.Domain == Domain
			&& HydraulicErosionField.IsValid() && HydraulicErosionField.Domain == Domain
			&& DepositionField.IsValid() && DepositionField.Domain == Domain
			&& EvaporationField.IsValid() && EvaporationField.Domain == Domain;
	}
};

class FTerrainContext
{
public:
	static void Analyze(
		const FTerrainHeightField& HeightField,
		float HeightScale,
		const TArray<float>& MountainMask,
		const TArray<float>& FoothillMask,
		const TArray<float>& PlainsMask,
		FTerrainContextMaps& OutContext);

	static void BuildProcessMasks(
		const FTerrainContextMaps& Context,
		const FTerrainHeightField& HeightField,
		float ThermalTalusAngleDegrees,
		const FTerrainProcessMaskSettings& Settings,
		FTerrainProcessMasks& OutMasks);
};
