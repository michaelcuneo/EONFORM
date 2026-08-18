#pragma once

#include "CoreMinimal.h"

class FTerrainShaping
{
public:
	static float BuildMountainMask(float MacroNoise, float Threshold, float TransitionWidth);
	static float ApplySignedPower(float Value, float Exponent);
};
