#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"

struct EONFORMCORE_API FEonformTerrainShapeSettings
{
	int32 Seed = 1337;
	float BaseStrength = 0.38f;

	bool bEnableMacroShape = true;
	float MacroFrequency = 0.000055f;
	int32 MacroOctaves = 3;
	float MacroStrength = 0.75f;
	float MacroContrast = 1.1f;

	bool bEnableMountainMask = true;
	float MountainThreshold = 0.12f;
	float MountainTransition = 0.55f;

	bool bEnableDomainWarp = true;
	float WarpFrequency = 0.00012f;
	float WarpStrength = 4500.0f;
	float WarpRegionality = 0.85f;

	bool bEnableRidges = true;
	float RidgeFrequency = 0.00032f;
	int32 RidgeOctaves = 4;
	float RidgeStrength = 0.55f;
	float RidgeSharpness = 1.8f;

	bool bEnableFoothills = true;
	float FoothillWidth = 0.55f;
	float FoothillStrength = 0.28f;
	float FoothillFrequency = 0.00018f;

	bool bEnableValleys = true;
	float ValleyFrequency = 0.000085f;
	float ValleyWidth = 0.22f;
	float ValleySharpness = 1.4f;
	float ValleyDepth = 0.16f;

	bool bEnablePlains = true;
	float PlainsStrength = 0.55f;
	float PlainsFlattenExponent = 1.65f;
	float PlainsRollingStrength = 0.08f;
	float PlainsRollingFrequency = 0.00012f;
};

/** Pure terrain-form shaping derived from the legacy EONFORM terrain generator. */
class EONFORMCORE_API FEonformTerrainShaping
{
public:
	static float BuildMountainMask(float MacroNoise, float Threshold, float TransitionWidth);
	static float BuildFoothillMask(float MountainMask, float Width);
	static float BuildValleyMask(float ValleyNoise, float Width, float Sharpness);
	static float ApplySignedPower(float Value, float Exponent);

	static bool Apply(
		const FEonformScalarField& InputHeight,
		const FEonformTerrainShapeSettings& Settings,
		FEonformTerrainDataset& InOutDataset,
		FString* OutError = nullptr);
};
