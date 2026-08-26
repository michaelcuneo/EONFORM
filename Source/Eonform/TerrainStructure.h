#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightField.h"

struct FTerrainStructuralSettings
{
	float DirectionDegrees = 32.0f;
	float DirectionVariation = 0.28f;
	float TectonicCoverage = 0.68f;

	float UpliftSpacing = 18000.0f;
	float UpliftWidth = 8500.0f;
	float UpliftStrength = 0.18f;
	float UpliftMountainBias = 0.32f;

	float LongValleySpacing = 18000.0f;
	float LongValleyWidth = 1800.0f;
	float LongValleyDepth = 240.0f;

	float FaultSpacing = 11000.0f;
	float FaultWidth = 500.0f;
	float FaultAngleOffsetDegrees = 22.0f;
	float FaultWeakness = 0.7f;

	float BeddingSpacing = 2400.0f;
	float BeddingContrast = 0.35f;
};

struct FTerrainStructuralMaps
{
	TArray<float> TectonicActivity;
	TArray<float> Uplift;
	TArray<float> LongValley;
	TArray<float> FaultWeakness;
	TArray<float> Bedding;

	bool IsValidFor(const FTerrainHeightField& HeightField) const
	{
		const int32 NumCells = HeightField.Data.Num();
		return HeightField.IsValid()
			&& TectonicActivity.Num() == NumCells
			&& Uplift.Num() == NumCells
			&& LongValley.Num() == NumCells
			&& FaultWeakness.Num() == NumCells
			&& Bedding.Num() == NumCells;
	}
};

class FTerrainStructure
{
public:
	static void Build(
		const FTerrainHeightField& HeightField,
		int32 Seed,
		const FTerrainStructuralSettings& Settings,
		FTerrainStructuralMaps& OutStructure);
};
