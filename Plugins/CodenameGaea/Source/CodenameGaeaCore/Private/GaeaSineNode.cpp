#include "GaeaSineNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor SineTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor SineNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	bool SineRemapHeight(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		FGaeaScalarField& OutField,
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
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Sine requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Sine terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!SineRemapHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Sine could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Sine produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaSineNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Sine;
	Descriptor.DisplayName = TEXT("Sine");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Remodulates terrain height through a sine curve.");
	Descriptor.Inputs.Add(SineTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SineTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(SineNumberParameter(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 1.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Sine, EvaluateSineNode);
}
