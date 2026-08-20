#include "GaeaElevationNode.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	bool EvaluateElevationNode(
		const FGaeaTerrainNode&,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Elevation requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainDerivedData::EnsureContext(
			Dataset,
			FMath::Max(Input->HeightScale, 1.0f),
			&Error))
		{
			return false;
		}

		const FGaeaScalarField* Elevation = Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
		if (!Elevation || !Elevation->IsValid())
		{
			Error = TEXT("Elevation could not derive a valid Elevation field.");
			return false;
		}

		Out.Outputs.Add(TEXT("Elevation"), FGaeaTerrainValue::MakeScalarField(*Elevation));
		return true;
	}
}

void RegisterGaeaElevationNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Elevation;
	Descriptor.DisplayName = TEXT("Elevation");
	Descriptor.Category = TEXT("Data");
	Descriptor.Description = TEXT("Derives a normalized elevation scalar field from terrain height.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Elevation"), TEXT("Elevation")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Elevation, EvaluateElevationNode);
}
