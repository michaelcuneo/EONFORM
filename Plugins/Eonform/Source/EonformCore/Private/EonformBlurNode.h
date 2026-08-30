#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainRecipe.h"

namespace EonformBlurNode
{
	/**
	 * Authoritative conversion from the Blur Radius control to a dependency
	 * radius in reference-lattice samples. Returns INDEX_NONE when a non-zero
	 * radius cannot be resolved because the reference dimensions are invalid.
	 */
	int32 ResolveRadiusSamples(
		const FEonformTerrainNode& Node,
		const FIntPoint& ReferenceDimensions);
}

void RegisterEonformBlurNode();
