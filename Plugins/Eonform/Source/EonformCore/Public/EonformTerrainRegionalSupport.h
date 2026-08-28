#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainRecipe.h"

struct EONFORMCORE_API FEonformTerrainRegionalSupportReport
{
	bool bSupported = false;
	TArray<FGuid> UnsupportedNodes;
	TArray<FString> Reasons;

	FString Describe() const;
};

/**
 * Conservative correctness gate for independent regional evaluation.
 *
 * Nodes are admitted only after their coordinate/global-dependency behaviour is
 * known. Unknown nodes fail closed so optimisation can never silently alter the
 * terrain. As more nodes gain region/halo/global-summary contracts they should
 * be promoted here alongside equivalence tests.
 */
class EONFORMCORE_API FEonformTerrainRegionalSupport
{
public:
	static FEonformTerrainRegionalSupportReport Analyze(const FEonformTerrainRecipe& Recipe);
};
