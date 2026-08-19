#include "TerrainDatasetBridge.h"

#include "GaeaTerrainFieldNames.h"
#include "TerrainContext.h"
#include "TerrainErosion.h"
#include "TerrainGeology.h"
#include "TerrainHeightField.h"

FGaeaTerrainDataset FTerrainDatasetBridge::Build(
	const FTerrainHeightField& HeightField,
	const FTerrainContextMaps* Context,
	const FTerrainProcessMasks* ProcessMasks,
	const FTerrainGeologyMaps* Geology,
	const FTerrainHydraulicErosionResult* HydraulicErosion)
{
	FGaeaTerrainDataset Dataset;
	if (!HeightField.IsValid())
	{
		return Dataset;
	}

	FGaeaScalarField Height = HeightField.ToGaeaScalarField(GaeaTerrainFieldNames::Height);
	Dataset.SetScalarField(MoveTemp(Height));

	if (Context && Context->IsValidFor(HeightField))
	{
		Dataset.SetScalarField(Context->ElevationField);
		Dataset.SetScalarField(Context->SlopeDegreesField);
		Dataset.SetScalarField(Context->ConcavityField);
		Dataset.SetScalarField(Context->ConvexityField);
		Dataset.SetScalarField(Context->MountainField);
		Dataset.SetScalarField(Context->FoothillField);
		Dataset.SetScalarField(Context->PlainsField);
	}

	if (ProcessMasks && ProcessMasks->IsValidFor(HeightField))
	{
		Dataset.SetScalarField(ProcessMasks->ThermalField);
		Dataset.SetScalarField(ProcessMasks->RainfallField);
		Dataset.SetScalarField(ProcessMasks->HydraulicErosionField);
		Dataset.SetScalarField(ProcessMasks->DepositionField);
		Dataset.SetScalarField(ProcessMasks->EvaporationField);
	}

	if (Geology && Geology->IsValidFor(HeightField))
	{
		Dataset.SetScalarField(Geology->RockHardnessField);
		Dataset.SetScalarField(Geology->WeatheringField);
		Dataset.SetScalarField(Geology->SoilDepthField);
	}

	if (HydraulicErosion && HydraulicErosion->IsValid())
	{
		Dataset.SetScalarField(HydraulicErosion->Wear);
		Dataset.SetScalarField(HydraulicErosion->Deposits);
		Dataset.SetScalarField(HydraulicErosion->Flow);
	}

	return Dataset;
}
