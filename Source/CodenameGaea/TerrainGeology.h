#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainContextMaps;
struct FTerrainStructuralMaps;

struct FTerrainGeologySettings
{
	float Frequency = 0.000045f;
	int32 Octaves = 3;
	float Contrast = 1.25f;
	float MountainHardnessBias = 0.18f;
	float PlainsSoftnessBias = 0.15f;
	float SoilFormationStrength = 0.65f;
	float FaultWeakeningStrength = 0.55f;
	float BeddingHardnessContrast = 0.2f;
};

struct FTerrainGeologyMaps
{
	FGaeaScalarField RockHardnessField;
	FGaeaScalarField WeatheringField;
	FGaeaScalarField SoilDepthField;

	TArray<float>& RockHardness;
	TArray<float>& Weathering;
	TArray<float>& SoilDepth;

	FTerrainGeologyMaps()
		: RockHardness(RockHardnessField.Values)
		, Weathering(WeatheringField.Values)
		, SoilDepth(SoilDepthField.Values)
	{
	}

	FTerrainGeologyMaps(const FTerrainGeologyMaps& Other)
		: RockHardnessField(Other.RockHardnessField)
		, WeatheringField(Other.WeatheringField)
		, SoilDepthField(Other.SoilDepthField)
		, RockHardness(RockHardnessField.Values)
		, Weathering(WeatheringField.Values)
		, SoilDepth(SoilDepthField.Values)
	{
	}

	FTerrainGeologyMaps& operator=(const FTerrainGeologyMaps& Other)
	{
		if (this != &Other)
		{
			RockHardnessField = Other.RockHardnessField;
			WeatheringField = Other.WeatheringField;
			SoilDepthField = Other.SoilDepthField;
		}
		return *this;
	}

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const FGaeaGridDomain& Domain = HeightField.GetGaeaDomain();
		return HeightField.IsValid()
			&& RockHardnessField.IsValid() && RockHardnessField.Domain == Domain
			&& WeatheringField.IsValid() && WeatheringField.Domain == Domain
			&& SoilDepthField.IsValid() && SoilDepthField.Domain == Domain;
	}
};

class FTerrainGeology
{
public:
	static void Build(
		const FTerrainHeightField& HeightField,
		const FTerrainContextMaps& Context,
		const FTerrainStructuralMaps* Structure,
		int32 Seed,
		const FTerrainGeologySettings& Settings,
		FTerrainGeologyMaps& OutGeology);
};
