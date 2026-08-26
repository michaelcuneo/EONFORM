#include "EonformInvertNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor InvertAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	bool InvertScalarField(const FEonformScalarField& Source, bool bTerrainHeight, FEonformScalarField& OutField)
	{
		if (!Source.IsValid()) return false;
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Value = Source.AtInterior(X, Y);
				OutField.AtInterior(X, Y) = bTerrainHeight ? -Value : 1.0f - FMath::Clamp(Value, 0.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateInvertNode(
		const FEonformTerrainNode&,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Invert requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!InvertScalarField(Input->ScalarField, false, Result))
			{
				Error = TEXT("Invert could not process the scalar field input.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Invert terrain input has no valid Height field.");
				return false;
			}

			FEonformScalarField InvertedHeight;
			if (!InvertScalarField(*Height, true, InvertedHeight))
			{
				Error = TEXT("Invert could not process the terrain Height field.");
				return false;
			}
			InvertedHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(InvertedHeight)))
			{
				Error = TEXT("Invert could not publish its Height field.");
				return false;
			}

			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Invert produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Invert received an unsupported input type.");
		return false;
	}
}

void RegisterEonformInvertNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Invert;
	Descriptor.DisplayName = TEXT("Invert");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Inverts terrain height or scalar mask values.");
	Descriptor.Inputs.Add(InvertAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(InvertAnyPort(TEXT("Out"), TEXT("Out")));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Invert, EvaluateInvertNode);
}
