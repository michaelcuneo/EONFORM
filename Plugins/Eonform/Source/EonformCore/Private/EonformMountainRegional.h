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
}
