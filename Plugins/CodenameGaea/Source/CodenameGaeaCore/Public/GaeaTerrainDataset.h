#pragma once

#include "CoreMinimal.h"
#include "GaeaScalarField.h"

/**
 * Owns named terrain fields produced by generation/evaluation.
 *
 * Fields are stored by value. Callers receive const field access so completed
 * datasets can be treated as immutable handoff objects by graph, editor,
 * runtime materialization, and gameplay-query systems.
 */
struct CODENAMEGAEACORE_API FGaeaTerrainDataset
{
	bool IsEmpty() const;
	int32 NumScalarFields() const;

	bool HasScalarField(FName Name) const;
	const FGaeaScalarField* FindScalarField(FName Name) const;

	bool SetScalarField(const FGaeaScalarField& Field);
	bool SetScalarField(FGaeaScalarField&& Field);
	bool RemoveScalarField(FName Name);
	void Reset();

	void GetScalarFieldNames(TArray<FName>& OutNames) const;

	bool SampleScalar(
		FName Name,
		const FVector2d& WorldPosition,
		float& OutValue,
		bool bClampToDomain = true) const;

private:
	TMap<FName, FGaeaScalarField> ScalarFields;
};
