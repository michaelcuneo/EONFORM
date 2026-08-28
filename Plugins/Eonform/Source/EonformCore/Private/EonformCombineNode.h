#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

namespace EonformCombine
{
	float ApplyMode(float A, float B, FName Mode);

	bool ApplyRawFields(
		const FEonformScalarField& A,
		const FEonformScalarField& B,
		FName Mode,
		float Ratio,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);
}

void RegisterEonformCombineNode();
