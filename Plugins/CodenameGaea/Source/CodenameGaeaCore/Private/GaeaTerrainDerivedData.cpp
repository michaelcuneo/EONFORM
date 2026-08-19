#include "GaeaTerrainDerivedData.h"

#include "GaeaTerrainFieldNames.h"

namespace
{
	bool HasContextFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::Elevation)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::SlopeDegrees)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Concavity)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Convexity)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Mountain)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Foothill)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Plains);
	}

	bool HasGeologyFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::RockHardness)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Weathering)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::SoilDepth);
	}

	bool HasProcessMaskFields(const FGaeaTerrainDataset& Dataset)
	{
		return Dataset.HasScalarField(GaeaTerrainFieldNames::Thermal)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Rainfall)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::HydraulicErosion)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Deposition)
			&& Dataset.HasScalarField(GaeaTerrainFieldNames::Evaporation);
	}

	const FGaeaScalarField* RequireHeight(const FGaeaTerrainDataset& Dataset, FString* OutError)
	{
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height && OutError)
		{
			*OutError = TEXT("Derived terrain data requires a Height field.");
		}
		return Height;
	}
}

bool FGaeaTerrainDerivedData::EnsureContext(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	FString* OutError)
{
	if (HasContextFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height)
	{
		return false;
	}

	return FGaeaTerrainContext::Analyze(
		*Height,
		FMath::Max(HeightScale, 1.0f),
		InOutDataset,
		OutError);
}

bool FGaeaTerrainDerivedData::EnsureGeology(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (HasGeologyFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	if (!EnsureContext(InOutDataset, HeightScale, OutError))
	{
		return false;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height)
	{
		return false;
	}

	return FGaeaTerrainGeology::Build(
		*Height,
		Settings.GeologySeed,
		Settings.Geology,
		InOutDataset,
		OutError);
}

bool FGaeaTerrainDerivedData::EnsureProcessMasks(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (HasProcessMaskFields(InOutDataset))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	if (!EnsureContext(InOutDataset, HeightScale, OutError))
	{
		return false;
	}

	const FGaeaScalarField* Height = RequireHeight(InOutDataset, OutError);
	if (!Height)
	{
		return false;
	}

	return FGaeaTerrainContext::BuildProcessMasks(
		*Height,
		Settings.ProcessMasks,
		InOutDataset,
		OutError);
}

bool FGaeaTerrainDerivedData::EnsureHydraulicInputs(
	FGaeaTerrainDataset& InOutDataset,
	float HeightScale,
	const FGaeaTerrainDerivedDataSettings& Settings,
	FString* OutError)
{
	if (!EnsureGeology(InOutDataset, HeightScale, Settings, OutError))
	{
		return false;
	}

	return EnsureProcessMasks(InOutDataset, HeightScale, Settings, OutError);
}
