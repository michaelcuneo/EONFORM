#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

namespace EonformTerrainProceduralOps
{
	struct FPreparedTerraceProfile
	{
		TArray<float> Levels;
		float Steepness = 0.2f;
		float Intensity = 1.0f;

		bool IsValid() const { return Levels.Num() >= 2; }
	};

	bool PrepareTerraceProfile(
		int32 NumTerraces,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FPreparedTerraceProfile& OutProfile,
		FString* OutError = nullptr);

	float ApplyPreparedTerraceValue(float Original, const FPreparedTerraceProfile& Profile);

	// Direct port of QuadSpinner.Gaea.Nodes.Profiles.Terrace for the
	// forceZero=false path used by Gaea 2.3.0.1 Landscapes.Ridge.
	bool TerraceFidelity(
		const FEonformScalarField& Source,
		int32 NumTerraces,
		float Uniformity,
		float Steepness,
		float Intensity,
		int32 Seed,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}
