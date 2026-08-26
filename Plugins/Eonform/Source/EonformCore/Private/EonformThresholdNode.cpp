#include "EonformThresholdNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor ThresholdAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor ThresholdNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	bool ThresholdProcessField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, bool bTerrain, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Threshold received an invalid field.");
			return false;
		}

		const float Level = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Level"), 0.5)), 0.0f, 1.0f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Value = Source.AtInterior(X, Y);
				if (bTerrain) Value = Value * 0.5f + 0.5f;
				Value = FMath::Clamp(Value, 0.0f, 1.0f);
				const float Result = Value >= Level ? 1.0f : 0.0f;
				OutField.AtInterior(X, Y) = bTerrain ? Result * 2.0f - 1.0f : Result;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateThresholdNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Threshold requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!ThresholdProcessField(Node, Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Threshold terrain input has no valid Height field.");
				return false;
			}

			FEonformScalarField ResultHeight;
			if (!ThresholdProcessField(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Threshold could not publish its Height field.");
				return false;
			}

			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Threshold produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Threshold received an unsupported input type.");
		return false;
	}
}

void RegisterEonformThresholdNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Threshold;
	Descriptor.DisplayName = TEXT("Threshold");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Converts terrain or mask values into a hard selection using a single cutoff level.");
	Descriptor.Inputs.Add(ThresholdAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(ThresholdAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ThresholdNumberParameter(TEXT("Level"), TEXT("Level"), 0.5, 0.0, 1.0));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Threshold, EvaluateThresholdNode);
}
