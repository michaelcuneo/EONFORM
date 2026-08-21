#include "GaeaSharpenNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor SharpenTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor SharpenNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor SharpenNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Edge"));
		Parameter.NameOptions.Add(TEXT("Frequency"));
		return Parameter;
	}

	float SharpenSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	bool SharpenHeightField(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, FGaeaScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Sharpen received an invalid Height field.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Edge"));
		if (Method != TEXT("Edge") && Method != TEXT("Frequency"))
		{
			Error = TEXT("Sharpen Method must be Edge or Frequency.");
			return false;
		}
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.5)), 0.0f, 2.0f);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Sum = 0.0f;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						Sum += SharpenSampleClamped(Source, X + DX, Y + DY);
					}
				}
				const float Center = Source.AtInterior(X, Y);
				const float Blurred = Sum / 9.0f;
				const float Detail = Center - Blurred;
				const float Gain = Method == TEXT("Frequency") ? 1.5f : 1.0f;
				OutField.AtInterior(X, Y) = FMath::Clamp(Center + Detail * Amount * Gain, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateSharpenNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Sharpen requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Sharpen terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!SharpenHeightField(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Sharpen could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Sharpen produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaSharpenNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Sharpen;
	Descriptor.DisplayName = TEXT("Sharpen");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Enhances terrain edges or high-frequency detail.");
	Descriptor.Inputs.Add(SharpenTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SharpenTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(SharpenNameParameter(TEXT("Method"), TEXT("Method"), TEXT("Edge")));
	Descriptor.Parameters.Add(SharpenNumberParameter(TEXT("Amount"), TEXT("Amount"), 0.5, 0.0, 2.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Sharpen, EvaluateSharpenNode);
}
