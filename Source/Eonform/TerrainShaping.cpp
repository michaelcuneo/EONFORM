#include "TerrainShaping.h"

float FTerrainShaping::BuildMountainMask(float MacroNoise, float Threshold, float TransitionWidth)
{
	const float SafeWidth = FMath::Max(TransitionWidth, UE_SMALL_NUMBER);
	const float Lower = Threshold - SafeWidth * 0.5f;
	const float Upper = Threshold + SafeWidth * 0.5f;
	const float T = FMath::Clamp((MacroNoise - Lower) / (Upper - Lower), 0.0f, 1.0f);

	return T * T * (3.0f - 2.0f * T);
}

float FTerrainShaping::BuildFoothillMask(float MountainMask, float Width)
{
	const float SafeWidth = FMath::Clamp(Width, 0.01f, 1.0f);
	const float DistanceFromEdge = FMath::Abs(MountainMask - 0.5f) * 2.0f;
	const float T = FMath::Clamp(1.0f - DistanceFromEdge / SafeWidth, 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float FTerrainShaping::BuildValleyMask(float ValleyNoise, float Width, float Sharpness)
{
	const float SafeWidth = FMath::Max(Width, 0.001f);
	const float SafeSharpness = FMath::Max(Sharpness, 0.01f);
	const float DistanceToCenter = FMath::Abs(ValleyNoise);
	const float T = FMath::Clamp(1.0f - DistanceToCenter / SafeWidth, 0.0f, 1.0f);
	return FMath::Pow(T, SafeSharpness);
}

float FTerrainShaping::ApplySignedPower(float Value, float Exponent)
{
	const float SafeExponent = FMath::Max(Exponent, UE_SMALL_NUMBER);
	return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), SafeExponent);
}
