#include "GaeaTerrainDataset.h"

bool FGaeaTerrainDataset::IsEmpty() const
{
	return ScalarFields.IsEmpty();
}

int32 FGaeaTerrainDataset::NumScalarFields() const
{
	return ScalarFields.Num();
}

bool FGaeaTerrainDataset::HasScalarField(FName Name) const
{
	return ScalarFields.Contains(Name);
}

const FGaeaScalarField* FGaeaTerrainDataset::FindScalarField(FName Name) const
{
	return ScalarFields.Find(Name);
}

bool FGaeaTerrainDataset::SetScalarField(const FGaeaScalarField& Field)
{
	if (!Field.IsValid() || Field.Descriptor.Name.IsNone())
	{
		return false;
	}

	ScalarFields.Add(Field.Descriptor.Name, Field);
	return true;
}

bool FGaeaTerrainDataset::SetScalarField(FGaeaScalarField&& Field)
{
	if (!Field.IsValid() || Field.Descriptor.Name.IsNone())
	{
		return false;
	}

	const FName Name = Field.Descriptor.Name;
	ScalarFields.Add(Name, MoveTemp(Field));
	return true;
}

bool FGaeaTerrainDataset::RemoveScalarField(FName Name)
{
	return ScalarFields.Remove(Name) > 0;
}

void FGaeaTerrainDataset::Reset()
{
	ScalarFields.Reset();
}

void FGaeaTerrainDataset::GetScalarFieldNames(TArray<FName>& OutNames) const
{
	ScalarFields.GetKeys(OutNames);
	OutNames.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});
}

bool FGaeaTerrainDataset::SampleScalar(
	FName Name,
	const FVector2d& WorldPosition,
	float& OutValue,
	bool bClampToDomain) const
{
	const FGaeaScalarField* Field = FindScalarField(Name);
	if (!Field)
	{
		return false;
	}

	OutValue = Field->SampleWorld(WorldPosition, bClampToDomain);
	return true;
}
