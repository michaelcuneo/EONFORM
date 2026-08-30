#pragma once

#include "EonformTerrainRecipe.h"

namespace EonformDenoiseNode
{
	inline int32 RequiredBorderSamples(const FEonformTerrainNode& Node)
	{
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		if (Amount <= UE_SMALL_NUMBER) return 0;
		const FName Type = Node.GetName(TEXT("Type"), TEXT("One Pass"));
		const int32 Passes = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Passes"), 1)), 1, 32);
		return Passes * (Type == TEXT("Two Pass") ? 2 : 1);
	}
}

void RegisterEonformDenoiseNode();
