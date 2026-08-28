#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainRecipe.h"

struct EONFORMCORE_API FEonformTerrainRegionalSupportReport
{
	bool bSupported = false;

	/**
	 * Number of full-resolution samples required around each requested region so
	 * every admitted neighbourhood operation can read its upstream dependency
	 * footprint without treating an internal tile edge as a world edge.
	 */
	int32 RequiredBorderSamples = 0;

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
 * be promoted here alongside equivalence tests. The same audit owns the graph's
 * required dependency margin; the Mesh Terrain scheduler must not guess one.
 */
class EONFORMCORE_API FEonformTerrainRegionalSupport
{
public:
	static FEonformTerrainRegionalSupportReport Analyze(const FEonformTerrainRecipe& Recipe);
};
