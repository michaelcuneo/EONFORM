#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainFractalWarp.h"

namespace EonformWarp
{
	bool Apply(
		const FEonformScalarField& Source,
		const EonformTerrainProceduralOps::FFractalWarpSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}

void RegisterEonformAuthoritativeWarpNode();
