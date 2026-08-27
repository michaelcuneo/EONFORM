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
