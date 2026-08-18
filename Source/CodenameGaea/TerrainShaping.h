#pragma once

#include "CoreMinimal.h"

class FTerrainShaping
{
public:
	static float BuildMountainMask(float MacroNoise, float Threshold, float TransitionWidth);
	static float BuildFoothillMask(float MountainMask, float Width);
	static float BuildValleyMask(float ValleyNoise, float Width, float Sharpness);
	static float ApplySignedPower(float Value, float Exponent);
};
