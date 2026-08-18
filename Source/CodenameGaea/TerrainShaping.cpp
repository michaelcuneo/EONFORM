#include "TerrainShaping.h"

float FTerrainShaping::BuildMountainMask(float MacroNoise, float Threshold, float TransitionWidth)
{
	const float SafeWidth = FMath::Max(TransitionWidth, UE_SMALL_NUMBER);
	const float Lower = Threshold - SafeWidth * 0.5f;
	const float Upper = Threshold + SafeWidth * 0.5f;
	const float T = FMath::Clamp((MacroNoise - Lower) / (Upper - Lower), 0.0f, 1.0f);

	return T * T * (3.0f - 2.0f * T);
}

float FTerrainShaping::ApplySignedPower(float Value, float Exponent)
{
	const float SafeExponent = FMath::Max(Exponent, UE_SMALL_NUMBER);
	return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), SafeExponent);
}
