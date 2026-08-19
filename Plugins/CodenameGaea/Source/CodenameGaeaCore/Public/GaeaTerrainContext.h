#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"

/** Pure terrain analysis that derives semantic context fields from Height. */
class CODENAMEGAEACORE_API FGaeaTerrainContext
{
public:
	static bool Analyze(
		const FGaeaScalarField& Height,
		float HeightScale,
		FGaeaTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
