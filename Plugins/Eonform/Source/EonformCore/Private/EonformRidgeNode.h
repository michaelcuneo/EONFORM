#pragma once

#include "CoreMinimal.h"
#include "EonformGridDomain.h"
#include "EonformScalarField.h"

class FEonformTerrainGlobalSummaryCache;

namespace EonformTerrainNodeTypes
{
	EONFORMCORE_API extern const FName Ridge;
}

struct FEonformRidgeSettings
{
	float Scale = 0.75f;
	float Height = 0.6f;
	float Definition = 0.4f;
	int32 Seed = 1337;
	float ScaleX = 1.0f;
	float ScaleY = 1.0f;
};

/**
 * Shared Ridge generator used by the public Ridge graph node and by compound
 * terrain generators such as Mountain. Resolution is carried by Domain; it is
 * never an artistic Ridge parameter.
 */
class EONFORMCORE_API FEonformRidgeGenerator
{
public:
	/** Legacy full-field reference implementation. */
	static bool Generate(
		const FEonformGridDomain& Domain,
		const FEonformRidgeSettings& Settings,
		FEonformScalarField& OutHeight,
		FString* OutError = nullptr);

	/**
	 * Exact streamed implementation. RidgeReferenceDomain is the virtual lattice
	 * the legacy Ridge generator would have built (including its 4097 cap).
	 * TargetDomain is any requested world-space window/resolution. Final regional
	 * generation shares one exact whole-Ridge minimum through SummaryCache.
	 */
	static bool GenerateRegional(
		const FEonformGridDomain& TargetDomain,
		const FEonformGridDomain& RidgeReferenceDomain,
		const FEonformRidgeSettings& Settings,
		bool bPreviewEvaluation,
		const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
		uint64 SummaryKey,
		FEonformScalarField& OutHeight,
		FString* OutError = nullptr);

	/**
	 * Evaluate exact final Ridge values at integer coordinates of the virtual
	 * full-world Ridge lattice. Compound nodes use this to resolve only the
	 * displaced dependency samples they actually need instead of materialising
	 * a guessed neighbourhood halo or duplicating Ridge internals.
	 */
	static bool SampleRegionalReference(
		const FEonformGridDomain& RidgeReferenceDomain,
		const FEonformRidgeSettings& Settings,
		const TArray<FIntPoint>& ReferenceCoordinates,
		const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>& SummaryCache,
		uint64 SummaryKey,
		TArray<float>& OutValues,
		FString* OutError = nullptr);
};

void RegisterEonformRidgeNode();
