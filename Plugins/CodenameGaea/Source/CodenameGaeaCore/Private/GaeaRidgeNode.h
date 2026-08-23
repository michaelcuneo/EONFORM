#pragma once

#include "CoreMinimal.h"
#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"

namespace GaeaTerrainNodeTypes
{
	CODENAMEGAEACORE_API extern const FName Ridge;
}

struct FGaeaRidgeSettings
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
class CODENAMEGAEACORE_API FGaeaRidgeGenerator
{
public:
	static bool Generate(
		const FGaeaGridDomain& Domain,
		const FGaeaRidgeSettings& Settings,
		FGaeaScalarField& OutHeight,
		FString* OutError = nullptr);
};

void RegisterGaeaRidgeNode();
