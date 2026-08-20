#include "GaeaTerrainRegionsNode.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainRegionsTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor TerrainRegionsScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	bool EvaluateTerrainRegionsNode(
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
			Error = TEXT("Terrain Regions requires a valid terrain input 'Terrain'.");
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

		const FGaeaScalarField* Mountain = Dataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
		const FGaeaScalarField* Foothill = Dataset.FindScalarField(GaeaTerrainFieldNames::Foothill);
		const FGaeaScalarField* Plains = Dataset.FindScalarField(GaeaTerrainFieldNames::Plains);
		if (!Mountain || !Mountain->IsValid()
			|| !Foothill || !Foothill->IsValid()
			|| !Plains || !Plains->IsValid())
		{
			Error = TEXT("Terrain Regions could not derive valid Mountain, Foothill, and Plains fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Mountain"), FGaeaTerrainValue::MakeScalarField(*Mountain));
		Out.Outputs.Add(TEXT("Foothill"), FGaeaTerrainValue::MakeScalarField(*Foothill));
		Out.Outputs.Add(TEXT("Plains"), FGaeaTerrainValue::MakeScalarField(*Plains));
		return true;
	}
}

void RegisterGaeaTerrainRegionsNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::TerrainRegions;
	Descriptor.DisplayName = TEXT("Terrain Regions");
	Descriptor.Category = TEXT("Data");
	Descriptor.Description = TEXT("Derives normalized Mountain, Foothill, and Plains regional masks from terrain context.");
	Descriptor.Inputs.Add(TerrainRegionsTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerrainRegionsScalarPort(TEXT("Mountain"), TEXT("Mountain")));
	Descriptor.Outputs.Add(TerrainRegionsScalarPort(TEXT("Foothill"), TEXT("Foothill")));
	Descriptor.Outputs.Add(TerrainRegionsScalarPort(TEXT("Plains"), TEXT("Plains")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::TerrainRegions, EvaluateTerrainRegionsNode);
}
