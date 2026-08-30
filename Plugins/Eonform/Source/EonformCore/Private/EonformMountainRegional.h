#pragma once

#include "CoreMinimal.h"
#include "EonformGridDomain.h"
#include "EonformRidgeNode.h"
#include "EonformScalarField.h"

class FEonformTerrainGlobalSummaryCache;

namespace EonformMountainRegional
{
	bool GenerateCore(
		const FEonformGridDomain& TargetDomain,
		const FEonformGridDomain& ReferenceDomain,
		const FEonformRidgeSettings& RidgeSettings0,
		const FEonformRidgeSettings& RidgeSettings1,
		const FEonformRidgeSettings& RidgeSettings2,
		float MountainScale,
		float XCenter,
		float YCenter,
		int32 WarpSeed,
		bool bApplyPreWarp,
		const FGuid& MountainNodeId,
		const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
		FEonformScalarField& OutHeight,
		FString* OutError = nullptr);

	/**
	 * Resolves the exact min/max of the virtual full-world Mountain core without
	 * allocating that full field. The reduction streams bounded full-width strips
	 * through GenerateCore using the shared global-summary strip budget and caches
	 * the result across all regions in the generation plan.
	 */
	bool ResolveCoreRange(
		const FEonformGridDomain& ReferenceDomain,
		const FEonformRidgeSettings& RidgeSettings0,
		const FEonformRidgeSettings& RidgeSettings1,
		const FEonformRidgeSettings& RidgeSettings2,
		float MountainScale,
		float XCenter,
		float YCenter,
		int32 WarpSeed,
		bool bApplyPreWarp,
		const FGuid& MountainNodeId,
		const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
		float& OutMinimum,
		float& OutMaximum,
		FString* OutError = nullptr);
}
