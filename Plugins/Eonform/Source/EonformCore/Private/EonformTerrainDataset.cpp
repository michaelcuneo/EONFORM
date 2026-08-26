#include "EonformTerrainDataset.h"

namespace
{
	const FName HeightFieldName(TEXT("Height"));

	bool IsIntrinsicHeightDerivedField(FName Name)
	{
		// These fields are analysis of the current Height surface and are therefore
		// never safe to carry across a Height replacement. Geology/material/process
		// fields are intentionally NOT listed here because they may be authored by
		// graph nodes and must not be silently discarded.
		return Name == TEXT("Elevation")
			|| Name == TEXT("SlopeDegrees")
			|| Name == TEXT("Concavity")
			|| Name == TEXT("Convexity")
			|| Name == TEXT("Mountain")
			|| Name == TEXT("Foothill")
			|| Name == TEXT("Plains")
			|| Name == TEXT("FlowDirection")
			|| Name == TEXT("FlowAccumulation")
			|| Name == TEXT("CatchmentAreaKm2")
			|| Name == TEXT("DistanceToOutletKm")
			|| Name == TEXT("StreamOrder");
	}
}

bool FEonformTerrainDataset::IsEmpty() const
{
	return ScalarFields.IsEmpty();
}

int32 FEonformTerrainDataset::NumScalarFields() const
{
	return ScalarFields.Num();
}

bool FEonformTerrainDataset::HasScalarField(FName Name) const
{
	return ScalarFields.Contains(Name);
}

const FEonformScalarField* FEonformTerrainDataset::FindScalarField(FName Name) const
{
	return ScalarFields.Find(Name);
}

bool FEonformTerrainDataset::SetScalarField(const FEonformScalarField& Field)
{
	if (!Field.IsValid() || Field.Descriptor.Name.IsNone())
	{
		return false;
	}

	const FName Name = Field.Descriptor.Name;
	if (Name == HeightFieldName)
	{
		InvalidateHeightDerivedFields();
	}
	ScalarFields.Add(Name, Field);
	if (IsIntrinsicHeightDerivedField(Name)) HeightDerivedFields.Add(Name);
	else HeightDerivedFields.Remove(Name);
	return true;
}

bool FEonformTerrainDataset::SetScalarField(FEonformScalarField&& Field)
{
	if (!Field.IsValid() || Field.Descriptor.Name.IsNone())
	{
		return false;
	}

	const FName Name = Field.Descriptor.Name;
	if (Name == HeightFieldName)
	{
		InvalidateHeightDerivedFields();
	}
	ScalarFields.Add(Name, MoveTemp(Field));
	if (IsIntrinsicHeightDerivedField(Name)) HeightDerivedFields.Add(Name);
	else HeightDerivedFields.Remove(Name);
	return true;
}

bool FEonformTerrainDataset::SetHeightDerivedScalarField(const FEonformScalarField& Field)
{
	if (!Field.IsValid() || Field.Descriptor.Name.IsNone() || Field.Descriptor.Name == HeightFieldName)
	{
		return false;
	}

	const FName Name = Field.Descriptor.Name;
	ScalarFields.Add(Name, Field);
	HeightDerivedFields.Add(Name);
	return true;
}

bool FEonformTerrainDataset::SetHeightDerivedScalarField(FEonformScalarField&& Field)
{
	if (!Field.IsValid() || Field.Descriptor.Name.IsNone() || Field.Descriptor.Name == HeightFieldName)
	{
		return false;
	}

	const FName Name = Field.Descriptor.Name;
	ScalarFields.Add(Name, MoveTemp(Field));
	HeightDerivedFields.Add(Name);
	return true;
}

bool FEonformTerrainDataset::IsHeightDerivedScalarField(FName Name) const
{
	return HeightDerivedFields.Contains(Name);
}

bool FEonformTerrainDataset::MarkScalarFieldHeightDerived(FName Name)
{
	if (Name.IsNone() || Name == HeightFieldName || !ScalarFields.Contains(Name))
	{
		return false;
	}
	HeightDerivedFields.Add(Name);
	return true;
}

int32 FEonformTerrainDataset::InvalidateHeightDerivedFields()
{
	int32 Removed = 0;
	const TArray<FName> Names = HeightDerivedFields.Array();
	for (const FName Name : Names)
	{
		Removed += ScalarFields.Remove(Name);
	}
	HeightDerivedFields.Reset();
	return Removed;
}

bool FEonformTerrainDataset::RemoveScalarField(FName Name)
{
	HeightDerivedFields.Remove(Name);
	return ScalarFields.Remove(Name) > 0;
}

void FEonformTerrainDataset::Reset()
{
	ScalarFields.Reset();
	HeightDerivedFields.Reset();
}

void FEonformTerrainDataset::GetScalarFieldNames(TArray<FName>& OutNames) const
{
	ScalarFields.GetKeys(OutNames);
	OutNames.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});
}

bool FEonformTerrainDataset::SampleScalar(
	FName Name,
	const FVector2d& WorldPosition,
	float& OutValue,
	bool bClampToDomain) const
{
	const FEonformScalarField* Field = FindScalarField(Name);
	if (!Field)
	{
		return false;
	}

	OutValue = Field->SampleWorld(WorldPosition, bClampToDomain);
	return true;
}
