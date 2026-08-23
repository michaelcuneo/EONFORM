#pragma once

#include "CoreMinimal.h"
#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"

namespace GaeaTerrainProceduralOps
{
	enum class EEdgeBehaviour : uint8
	{
		Edge,
		Mirror
	};

	struct FVoronoiSettings
	{
		float Scale = 0.5f;
		FName Function = TEXT("Euclidean");
		FName Form = TEXT("P");
		float Gain = 0.5f;
		FName WarpType = TEXT("Complex");
		float WarpFrequency = 0.05f;
		float WarpAmplitude = 0.5f;
		int32 WarpOctaves = 14;
		int32 Seed = 0;
		float X = 0.0f;
		float Y = 0.0f;
		float ScaleX = 1.0f;
		float ScaleY = 1.0f;
		float Jitter = 0.45f;
	};

	struct FPerlinSettings
	{
		float Scale = 0.5f;
		int32 Octaves = 10;
		float Gain = 0.5f;
		FName Type = TEXT("FBM");
		int32 Seed = 0;
		FName WarpType = TEXT("Complex");
		float WarpFrequency = 0.05f;
		float WarpAmplitude = 0.5f;
		int32 WarpOctaves = 10;
		float ScaleX = 1.0f;
		float ScaleY = 1.0f;
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct FFractalWarpSettings
	{
		float Size = 0.5f;
		float Strength = 0.5f;
		bool bPersistStrength = true;
		float ZScale = 0.0f;
		FName NoiseType = TEXT("Perlin FBM");
		float Perturbation = 0.5f;
		int32 Octaves = 12;
		float Roughness = 0.4f;
		bool bNormalized = false;
		int32 Iterations = 1;
		FName Mode = TEXT("Vector Field");
		EEdgeBehaviour EdgeBehaviour = EEdgeBehaviour::Mirror;
		int32 Seed = 0;
		const FGaeaScalarField* Modulator = nullptr;
		float Modulation = 0.0f;
		float ModulationDirectionDegrees = 45.0f;
		float Jitter = 0.45f;
	};

	bool GenerateVoronoi(const FGaeaGridDomain& Domain, const FVoronoiSettings& Settings, FGaeaScalarField& OutField, FString* OutError = nullptr);
	bool GeneratePerlin(const FGaeaGridDomain& Domain, const FPerlinSettings& Settings, FGaeaScalarField& OutField, FString* OutError = nullptr);
	bool ApplyTerrace(const FGaeaScalarField& Source, int32 NumTerraces, float Uniformity, float Steepness, float Intensity, int32 Seed, bool bForceZero, FGaeaScalarField& OutField, FString* OutError = nullptr);
	bool DirectionWarpPixels(const FGaeaScalarField& Source, const FGaeaScalarField& Custom, float StrengthPixels, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FGaeaScalarField& OutField, FString* OutError = nullptr);
	bool DirectionWarpNormalized(const FGaeaScalarField& Source, const FGaeaScalarField& Custom, float Strength, float DirectionDegrees, EEdgeBehaviour EdgeBehaviour, FGaeaScalarField& OutField, FString* OutError = nullptr);
	bool FractalWarp(const FGaeaScalarField& Source, const FFractalWarpSettings& Settings, FGaeaScalarField& OutField, FString* OutError = nullptr);

	void ApplyRadialGradientMultiply(FGaeaScalarField& Field, float CenterX, float CenterY, float RadiusPixels, float Height = 1.0f);
	void NormalizePositive(FGaeaScalarField& Field);
}
