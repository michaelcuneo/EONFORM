#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"

/**
 * Owns named terrain fields produced by generation/evaluation.
 *
 * Fields are stored by value. Callers receive const field access so completed
 * datasets can be treated as immutable handoff objects by graph, editor,
 * runtime materialization, and gameplay-query systems.
 *
 * Height-derived fields carry lightweight provenance. Replacing Height
 * invalidates only fields explicitly marked as derived from that Height,
 * while authored masks/geology/process data remain intact.
 */
struct EONFORMCORE_API FEonformTerrainDataset
{
	bool IsEmpty() const;
	int32 NumScalarFields() const;

	bool HasScalarField(FName Name) const;
	const FEonformScalarField* FindScalarField(FName Name) const;

	/** Publishes an authored/general field. Publishing Height invalidates stale height-derived fields. */
	bool SetScalarField(const FEonformScalarField& Field);
	bool SetScalarField(FEonformScalarField&& Field);

	/** Publishes a field whose values depend on the current Height field. */
	bool SetHeightDerivedScalarField(const FEonformScalarField& Field);
	bool SetHeightDerivedScalarField(FEonformScalarField&& Field);

	bool IsHeightDerivedScalarField(FName Name) const;
	bool MarkScalarFieldHeightDerived(FName Name);
	int32 InvalidateHeightDerivedFields();

	bool RemoveScalarField(FName Name);
	void Reset();

	void GetScalarFieldNames(TArray<FName>& OutNames) const;

	bool SampleScalar(
		FName Name,
		const FVector2d& WorldPosition,
		float& OutValue,
		bool bClampToDomain = true) const;

private:
	TMap<FName, FEonformScalarField> ScalarFields;
	TSet<FName> HeightDerivedFields;
};
