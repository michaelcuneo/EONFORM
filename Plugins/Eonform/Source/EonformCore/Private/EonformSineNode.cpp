#include "EonformSineNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor SineTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor SineNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
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

	bool SineRemapHeight(
		const FEonformTerrainNode& Node,
		const FEonformScalarField& Source,
		FEonformScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Sine received an invalid Height field.");
			return false;
		}

		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 1.0f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float SignedHeight = FMath::Clamp(Source.AtInterior(X, Y), -1.0f, 1.0f);
				const float Normalized = SignedHeight * 0.5f + 0.5f;
				const float Remodulated = 0.5f - 0.5f * FMath::Cos(Normalized * PI);
				const float ResultNormalized = FMath::Lerp(Normalized, Remodulated, Amount);
				OutField.AtInterior(X, Y) = FMath::Clamp(ResultNormalized * 2.0f - 1.0f, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateSineNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Sine requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Sine terrain input has no valid Height field.");
			return false;
		}

		FEonformScalarField ResultHeight;
		if (!SineRemapHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Sine could not publish its Height field.");
			return false;
		}

		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Sine produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformSineNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Sine;
	Descriptor.DisplayName = TEXT("Sine");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Remodulates terrain height through a sine curve.");
	Descriptor.Inputs.Add(SineTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SineTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(SineNumberParameter(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 1.0));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Sine, EvaluateSineNode);
}
