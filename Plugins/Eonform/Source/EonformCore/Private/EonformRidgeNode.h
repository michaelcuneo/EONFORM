#pragma once

#include "CoreMinimal.h"
#include "EonformGridDomain.h"
#include "EonformScalarField.h"

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
	static bool Generate(
		const FEonformGridDomain& Domain,
		const FEonformRidgeSettings& Settings,
		FEonformScalarField& OutHeight,
		FString* OutError = nullptr);
};

void RegisterEonformRidgeNode();
