#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainRecipe.h"

struct EONFORMCORE_API FEonformTerrainRegionalSupportReport
{
	bool bSupported = false;
	int32 RequiredBorderSamples = 0;
	TArray<FGuid> UnsupportedNodes;
	TArray<FString> Reasons;

	FString Describe() const;
};

class EONFORMCORE_API FEonformTerrainRegionalSupport
{
public:
	static FEonformTerrainRegionalSupportReport Analyze(const FEonformTerrainRecipe& Recipe);

	/**
	 * Resolution-aware regional audit used by runtime materialization. Local
	 * neighbourhood nodes derive their exact dependency radius from the same
	 * full-world reference lattice used during evaluation.
	 */
	static FEonformTerrainRegionalSupportReport Analyze(
		const FEonformTerrainRecipe& Recipe,
		const FIntPoint& ReferenceResolution);
};
