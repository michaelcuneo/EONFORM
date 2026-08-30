#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

namespace EonformAutoLevelNode
{
	/**
	 * Authoritative AutoLevel remap. Terrain values map to [-1,1], scalar masks
	 * to [0,1]. A degenerate source range preserves the input unchanged.
	 */
	bool ApplyRange(
		const FEonformScalarField& Source,
		bool bTerrain,
		float SourceMinimum,
		float SourceMaximum,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}

void RegisterEonformAutoLevelNode();
