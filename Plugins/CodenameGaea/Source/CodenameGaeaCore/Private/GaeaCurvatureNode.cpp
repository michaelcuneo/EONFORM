#include "GaeaCurvatureNode.h"

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

	bool EvaluateCurvatureNode(
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
			Error = TEXT("Curvature requires a valid terrain input 'Terrain'.");
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

		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if (!Concavity || !Concavity->IsValid() || !Convexity || !Convexity->IsValid())
		{
			Error = TEXT("Curvature could not derive valid Concavity and Convexity fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Concavity"), FGaeaTerrainValue::MakeScalarField(*Concavity));
		Out.Outputs.Add(TEXT("Convexity"), FGaeaTerrainValue::MakeScalarField(*Convexity));
		return true;
	}
}

void RegisterGaeaCurvatureNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Curvature;
	Descriptor.DisplayName = TEXT("Curvature");
	Descriptor.Category = TEXT("Data");
	Descriptor.Description = TEXT("Derives concave and convex curvature fields from terrain height.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Concavity"), TEXT("Concavity")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Convexity"), TEXT("Convexity")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Curvature, EvaluateCurvatureNode);
}
