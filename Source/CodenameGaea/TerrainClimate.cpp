#include "TerrainClimate.h"

#include "TerrainContext.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}

void FTerrainClimate::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainContextMaps& Context,
	float HeightScale,
	const FTerrainClimateSettings& Settings,
	FTerrainClimateMaps& OutClimate)
{
	OutClimate = FTerrainClimateMaps{};
	if (!HeightField.IsValid() || !Context.IsValidFor(HeightField) || HeightScale <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 Resolution = HeightField.Resolution;
	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = HeightField.WorldSize / static_cast<float>(Resolution - 1);
	const float DirectionRadians = FMath::DegreesToRadians(Settings.PrevailingWindDirectionDegrees);
	const FVector2D WindDirection(FMath::Cos(DirectionRadians), FMath::Sin(DirectionRadians));
	const bool bSweepX = FMath::Abs(WindDirection.X) >= FMath::Abs(WindDirection.Y);
	const int32 PrimaryStep = bSweepX ? (WindDirection.X >= 0.0f ? 1 : -1) : (WindDirection.Y >= 0.0f ? 1 : -1);

	OutClimate.TemperatureC.SetNumZeroed(NumCells);
	OutClimate.Precipitation.SetNumZeroed(NumCells);
	OutClimate.Humidity.SetNumZeroed(NumCells);
	OutClimate.EvaporationPotential.SetNumZeroed(NumCells);
	OutClimate.SnowPotential.SetNumZeroed(NumCells);

	const float BaseHumidity = FMath::Clamp(Settings.BaseHumidity, 0.0f, 1.0f);
	const float OrographicStrength = FMath::Max(Settings.OrographicStrength, 0.0f);
	const float RainShadowStrength = FMath::Max(Settings.RainShadowStrength, 0.0f);
	const float MoistureRecovery = FMath::Clamp(Settings.MoistureRecovery, 0.0f, 1.0f);

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float ElevationCm = HeightField.Data[Index] * HeightScale;
		const float ElevationKm = FMath::Max(ElevationCm, 0.0f) / 100000.0f;
		const float Temperature = Settings.BaseTemperatureC - ElevationKm * Settings.LapseRateCPerKm;
		OutClimate.TemperatureC[Index] = Temperature;

		const float Warmth = SmoothStep01((Temperature + 5.0f) / 35.0f);
		const float Dryness = 1.0f - BaseHumidity;
		OutClimate.EvaporationPotential[Index] = FMath::Clamp(Warmth * (0.35f + Dryness * 0.65f), 0.0f, 1.0f);
		OutClimate.SnowPotential[Index] = SmoothStep01((Settings.SnowTemperatureC - Temperature + 2.0f) / 6.0f);
	}

	const int32 PrimaryCount = Resolution;
	const int32 SecondaryCount = Resolution;

	for (int32 Secondary = 0; Secondary < SecondaryCount; ++Secondary)
	{
		float AirMoisture = BaseHumidity;
		float PreviousHeight = 0.0f;
		bool bHasPrevious = false;

		for (int32 PrimaryOffset = 0; PrimaryOffset < PrimaryCount; ++PrimaryOffset)
		{
			const int32 Primary = PrimaryStep > 0 ? PrimaryOffset : (PrimaryCount - 1 - PrimaryOffset);
			const int32 X = bSweepX ? Primary : Secondary;
			const int32 Y = bSweepX ? Secondary : Primary;
			const int32 Index = HeightField.Index(X, Y);
			const float CurrentHeight = HeightField.Data[Index] * HeightScale;

			float Rise = 0.0f;
			float Descent = 0.0f;
			if (bHasPrevious)
			{
				const float Delta = (CurrentHeight - PreviousHeight) / FMath::Max(CellSize, UE_SMALL_NUMBER);
				Rise = FMath::Max(Delta, 0.0f);
				Descent = FMath::Max(-Delta, 0.0f);
			}

			const float WindwardLift = FMath::Clamp(Rise * OrographicStrength, 0.0f, 1.0f);
			const float ShadowDrying = FMath::Clamp(Descent * RainShadowStrength, 0.0f, 1.0f);
			const float MountainLift = Context.Mountain[Index] * 0.18f + Context.Foothill[Index] * 0.08f;
			const float RainFraction = FMath::Clamp(0.04f + WindwardLift + MountainLift, 0.0f, 0.85f);
			const float Rain = AirMoisture * RainFraction;

			OutClimate.Precipitation[Index] = FMath::Clamp(Rain + BaseHumidity * 0.08f, 0.0f, 1.0f);
			AirMoisture = FMath::Max(0.0f, AirMoisture - Rain);
			AirMoisture *= 1.0f - ShadowDrying * 0.35f;
			AirMoisture = FMath::Lerp(AirMoisture, BaseHumidity, MoistureRecovery);
			OutClimate.Humidity[Index] = FMath::Clamp(AirMoisture, 0.0f, 1.0f);

			PreviousHeight = CurrentHeight;
			bHasPrevious = true;
		}
	}

	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		const float ClimateWetness = FMath::Clamp(OutClimate.Precipitation[Index] * 0.75f + OutClimate.Humidity[Index] * 0.25f, 0.0f, 1.0f);
		OutClimate.EvaporationPotential[Index] *= 1.0f - ClimateWetness * 0.45f;
	}
}
