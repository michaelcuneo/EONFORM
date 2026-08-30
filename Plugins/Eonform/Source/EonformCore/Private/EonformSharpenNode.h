#pragma once

#include "EonformTerrainRecipe.h"

namespace EonformSharpenNode
{
	inline int32 RequiredBorderSamples(const FEonformTerrainNode& Node)
	{
		return FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 2.0f) <= UE_SMALL_NUMBER ? 0 : 1;
	}
}

void RegisterEonformSharpenNode();
