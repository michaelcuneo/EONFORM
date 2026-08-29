#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

namespace EonformDirectionalWarp
{
	enum class EEdgeBehaviour : uint8
	{
		Edge,
		Mirror
	};

	/** Resolve the exact continuous source coordinate for one DirectionalWarp lattice sample. */
	FVector2D ResolveSourceCoordinate(
		const FVector2D& LatticeCoordinate,
		float CustomValue,
		float StrengthPixels,
		float DirectionDegrees);

	/**
	 * Authoritative bilinear sampler used by DirectionalWarp. SampleInteger is
	 * evaluated only at resolved full-world integer lattice coordinates.
	 */
	float SampleBilinear(
		const TFunctionRef<float(int32, int32)>& SampleInteger,
		const FIntPoint& Dimensions,
		float X,
		float Y,
		EEdgeBehaviour EdgeBehaviour);

	bool ApplyPixels(
		const FEonformScalarField& Source,
		const FEonformScalarField& Custom,
		float StrengthPixels,
		float DirectionDegrees,
		EEdgeBehaviour EdgeBehaviour,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	bool ApplyNormalized(
		const FEonformScalarField& Source,
		const FEonformScalarField& Custom,
		float Strength,
		float DirectionDegrees,
		EEdgeBehaviour EdgeBehaviour,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}

void RegisterEonformDirectionalWarpNode();
