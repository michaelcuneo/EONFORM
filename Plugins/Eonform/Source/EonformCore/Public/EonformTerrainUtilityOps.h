#pragma once

#include "CoreMinimal.h"
#include "EonformColorField.h"
#include "EonformScalarField.h"

/** Settings for the reusable Math utility operation. */
struct EONFORMCORE_API FEonformTerrainUtilityMathSettings
{
	FString Expression = TEXT("a");
	bool bNormalizedCoordinates = true;
};

/** One color layer consumed by the reusable Mixer operation. */
struct EONFORMCORE_API FEonformTerrainUtilityColorLayer
{
	const FEonformColorField* Color = nullptr;
	const FEonformScalarField* Mask = nullptr;
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
class EONFORMCORE_API FEonformTerrainUtilityOps
{
public:
	/** Evaluates a safe per-sample expression using a, b, c, x and y. */
	static bool EvaluateMath(
		const FEonformScalarField& A,
		const FEonformScalarField* B,
		const FEonformScalarField* C,
		const FEonformTerrainUtilityMathSettings& Settings,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	/** Produces a normalized comparison/difference map between two fields. */
	static bool Compare(
		const FEonformScalarField& A,
		const FEonformScalarField& B,
		float Ratio,
		bool bPerpendicular,
		bool bSwap,
		FEonformScalarField& OutField,
		FEonformColorField* OutColorized = nullptr,
		FString* OutError = nullptr);

	/** Post-masks an already-applied effect against its pre-effect source. */
	static bool ApplyMask(
		const FEonformScalarField& After,
		const FEonformScalarField& Before,
		const FEonformScalarField& Mask,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	/** Converts a scalar/terrain field into an edge-blended tileable field. */
	static bool MakeSeamless(
		const FEonformScalarField& Input,
		float Edge,
		float ShiftX,
		float ShiftY,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	/** Converts a color field into an edge-blended tileable color field. */
	static bool MakeSeamless(
		const FEonformColorField& Input,
		float Edge,
		float ShiftX,
		float ShiftY,
		FEonformColorField& OutField,
		FString* OutError = nullptr);

	/** Tiles a scalar/terrain field repeatedly across its existing domain. */
	static bool Repeat(
		const FEonformScalarField& Input,
		int32 Tiles,
		bool bCompensateHeight,
		float Height,
		FEonformScalarField& OutField,
		FString* OutError = nullptr);

	/** Blends an arbitrary number of color layers with optional scalar masks. */
	static bool MixColors(
		const TArray<FEonformTerrainUtilityColorLayer>& Layers,
		FEonformColorField& OutField,
		FString* OutError = nullptr);

	/** Deterministic seed mutation shared by Reseed-aware composite operations. */
	static int32 MutateSeed(int32 BaseSeed, int32 OverrideSeed);
};