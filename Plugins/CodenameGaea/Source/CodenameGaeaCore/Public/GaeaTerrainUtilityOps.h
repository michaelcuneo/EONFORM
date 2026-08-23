#pragma once

#include "CoreMinimal.h"
#include "GaeaColorField.h"
#include "GaeaScalarField.h"

/** Settings for the reusable Math utility operation. */
struct CODENAMEGAEACORE_API FGaeaTerrainUtilityMathSettings
{
	FString Expression = TEXT("a");
	bool bNormalizedCoordinates = true;
};

/** One color layer consumed by the reusable Mixer operation. */
struct CODENAMEGAEACORE_API FGaeaTerrainUtilityColorLayer
{
	const FGaeaColorField* Color = nullptr;
	const FGaeaScalarField* Mask = nullptr;
	float Opacity = 1.0f;
	FName BlendMode = TEXT("Blend");
};

/**
 * Reusable data/mask/routing operations used by both public Utility nodes and
 * EONFORM's compiled Terrain composites.
 *
 * Public node evaluators should remain thin wrappers over this layer so a
 * Mountain/Island/etc composite does not secretly use a second implementation.
 */
class CODENAMEGAEACORE_API FGaeaTerrainUtilityOps
{
public:
	/** Evaluates a safe per-sample expression using a, b, c, x and y. */
	static bool EvaluateMath(
		const FGaeaScalarField& A,
		const FGaeaScalarField* B,
		const FGaeaScalarField* C,
		const FGaeaTerrainUtilityMathSettings& Settings,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);

	/** Produces a normalized comparison/difference map between two fields. */
	static bool Compare(
		const FGaeaScalarField& A,
		const FGaeaScalarField& B,
		float Ratio,
		bool bPerpendicular,
		bool bSwap,
		FGaeaScalarField& OutField,
		FGaeaColorField* OutColorized = nullptr,
		FString* OutError = nullptr);

	/** Post-masks an already-applied effect against its pre-effect source. */
	static bool ApplyMask(
		const FGaeaScalarField& After,
		const FGaeaScalarField& Before,
		const FGaeaScalarField& Mask,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);

	/** Converts a scalar/terrain field into an edge-blended tileable field. */
	static bool MakeSeamless(
		const FGaeaScalarField& Input,
		float Edge,
		float ShiftX,
		float ShiftY,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);

	/** Converts a color field into an edge-blended tileable color field. */
	static bool MakeSeamless(
		const FGaeaColorField& Input,
		float Edge,
		float ShiftX,
		float ShiftY,
		FGaeaColorField& OutField,
		FString* OutError = nullptr);

	/** Tiles a scalar/terrain field repeatedly across its existing domain. */
	static bool Repeat(
		const FGaeaScalarField& Input,
		int32 Tiles,
		bool bCompensateHeight,
		float Height,
		FGaeaScalarField& OutField,
		FString* OutError = nullptr);

	/** Blends an arbitrary number of color layers with optional scalar masks. */
	static bool MixColors(
		const TArray<FGaeaTerrainUtilityColorLayer>& Layers,
		FGaeaColorField& OutField,
		FString* OutError = nullptr);

	/** Deterministic seed mutation shared by Reseed-aware composite operations. */
	static int32 MutateSeed(int32 BaseSeed, int32 OverrideSeed);
};