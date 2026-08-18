#include "TerrainBiomes.h"

#include "TerrainClimate.h"
#include "TerrainContext.h"
#include "TerrainGeology.h"
#include "TerrainParallel.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}

void FTerrainBiomes::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainContextMaps& Context,
	const FTerrainClimateMaps& Climate,
	const FTerrainGeologyMaps& Geology,
	const TArray<float>& WetnessMask,
	const TArray<float>& RiverMask,
	const FTerrainBiomeSettings& Settings,
	FTerrainBiomeMaps& OutBiomes)
{
	OutBiomes = FTerrainBiomeMaps{};
	if (!HeightField.IsValid()
		|| !Context.IsValidFor(HeightField)
		|| !Climate.IsValidFor(HeightField)
		|| !Geology.IsValidFor(HeightField))
	{
		return;
	}

	const int32 NumCells = HeightField.Data.Num();
	OutBiomes.Forest.SetNumZeroed(NumCells);
	OutBiomes.Grassland.SetNumZeroed(NumCells);
	OutBiomes.Arid.SetNumZeroed(NumCells);
	OutBiomes.Alpine.SetNumZeroed(NumCells);
	OutBiomes.Wetland.SetNumZeroed(NumCells);
	OutBiomes.ExposedRock.SetNumZeroed(NumCells);
	OutBiomes.Snow.SetNumZeroed(NumCells);

	const bool bHasWetness = WetnessMask.Num() == NumCells;
	const bool bHasRivers = RiverMask.Num() == NumCells;

	TerrainParallel::ForRange(TEXT("TerrainBiomes"), NumCells, 65536, [&](int32 Start, int32 End)
	{
		for (int32 Index = Start; Index < End; ++Index)
		{
			const float Elevation = Context.Elevation[Index];
			const float Slope = Context.SlopeDegrees[Index];
			const float Soil = Geology.SoilDepth[Index];
			const float Hardness = Geology.RockHardness[Index];
			const float Weathering = Geology.Weathering[Index];
			const float Rain = Climate.Precipitation[Index];
			const float Humidity = Climate.Humidity[Index];
			const float Temperature = Climate.TemperatureC[Index];
			const float SnowPotential = Climate.SnowPotential[Index];
			const float Wetness = bHasWetness ? WetnessMask[Index] : 0.0f;
			const float River = bHasRivers ? RiverMask[Index] : 0.0f;

			const float Moisture = FMath::Clamp(Rain * 0.52f + Humidity * 0.23f + Wetness * 0.25f, 0.0f, 1.0f);
			const float GentleSlope = 1.0f - SmoothStep01(Slope / FMath::Max(Settings.ForestMaxSlopeDegrees, 1.0f));
			const float Temperate = 1.0f - FMath::Clamp(FMath::Abs(Temperature - 16.0f) / 28.0f, 0.0f, 1.0f);
			const float ForestMoisture = SmoothStep01((Moisture - Settings.ForestMoistureThreshold + 0.12f) / 0.28f);
			OutBiomes.Forest[Index] = FMath::Clamp(ForestMoisture * Temperate * GentleSlope * (0.35f + Soil * 0.65f), 0.0f, 1.0f);

			const float GrassMoisture = 1.0f - FMath::Clamp(FMath::Abs(Moisture - 0.46f) / 0.36f, 0.0f, 1.0f);
			const float ForestSuppression = 1.0f - OutBiomes.Forest[Index] * 0.75f;
			OutBiomes.Grassland[Index] = FMath::Clamp(GrassMoisture * Temperate * GentleSlope * ForestSuppression, 0.0f, 1.0f);

			const float Dryness = SmoothStep01((Settings.AridMoistureThreshold - Moisture + 0.12f) / 0.26f);
			const float Warm = SmoothStep01((Temperature + 2.0f) / 28.0f);
			OutBiomes.Arid[Index] = FMath::Clamp(Dryness * Warm * (0.45f + Context.Plains[Index] * 0.55f), 0.0f, 1.0f);

			const float AlpineElevation = SmoothStep01((Elevation - Settings.AlpineElevationThreshold + 0.08f) / 0.22f);
			const float Cold = SmoothStep01((9.0f - Temperature) / 14.0f);
			OutBiomes.Alpine[Index] = FMath::Clamp(FMath::Max(AlpineElevation, Cold * Context.Mountain[Index]) * (1.0f - Soil * 0.4f), 0.0f, 1.0f);

			const float WetlandWater = FMath::Max(Wetness, River * 0.9f);
			const float WetlandThreshold = SmoothStep01((WetlandWater - Settings.WetlandWetnessThreshold + 0.1f) / 0.24f);
			const float LowSlope = 1.0f - SmoothStep01(Slope / 10.0f);
			OutBiomes.Wetland[Index] = FMath::Clamp(WetlandThreshold * LowSlope * (0.4f + Soil * 0.6f), 0.0f, 1.0f);

			const float SteepRock = SmoothStep01((Slope - 24.0f) / 22.0f);
			const float ThinSoil = 1.0f - Soil;
			const float ResistantRock = FMath::Clamp(Hardness * 0.75f + (1.0f - Weathering) * 0.25f, 0.0f, 1.0f);
			OutBiomes.ExposedRock[Index] = FMath::Clamp(SteepRock * ThinSoil * ResistantRock, 0.0f, 1.0f);

			OutBiomes.Snow[Index] = FMath::Clamp(SnowPotential * (0.45f + Elevation * 0.55f) * (1.0f - River * 0.7f), 0.0f, 1.0f);
		}
	});
}
