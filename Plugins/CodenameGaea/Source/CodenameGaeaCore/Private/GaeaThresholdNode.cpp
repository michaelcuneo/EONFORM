#include "GaeaThresholdNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ThresholdAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ThresholdNumberParameter(
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

	FGaeaTerrainParameterDescriptor ThresholdBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float ThresholdSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		const int32 SX = FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1);
		const int32 SY = FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1);
		return Field.AtInterior(SX, SY);
	}

	float ThresholdLocalMean(const FGaeaScalarField& Field, int32 X, int32 Y, bool bTerrain)
	{
		float Sum = 0.0f;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				float Value = ThresholdSampleClamped(Field, X + DX, Y + DY);
				if (bTerrain)
				{
					Value = Value * 0.5f + 0.5f;
				}
				Sum += FMath::Clamp(Value, 0.0f, 1.0f);
			}
		}
		return Sum / 9.0f;
	}

	bool ThresholdProcessField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		bool bTerrain,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Threshold received an invalid field.");
			return false;
		}

		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.0f, 1.0f);
		const float Bias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Bias"), 0.0)), -1.0f, 1.0f);
		const bool bAdaptive = Node.GetBool(TEXT("Adaptive"), false);
		const bool bInvert = Node.GetBool(TEXT("Invert"), false);

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Value = Source.AtInterior(X, Y);
				if (bTerrain)
				{
					Value = Value * 0.5f + 0.5f;
				}
				Value = FMath::Clamp(Value, 0.0f, 1.0f);

				float Limit = Scale;
				if (bAdaptive)
				{
					Limit = FMath::Clamp(ThresholdLocalMean(Source, X, Y, bTerrain) + Bias, 0.0f, 1.0f);
				}

				float Result = Value >= Limit ? 1.0f : 0.0f;
				if (bInvert)
				{
					Result = 1.0f - Result;
				}
				OutField.AtInterior(X, Y) = bTerrain ? Result * 2.0f - 1.0f : Result;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateThresholdNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Threshold requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!ThresholdProcessField(Node, Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Threshold terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField ResultHeight;
			if (!ThresholdProcessField(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Threshold could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
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

void RegisterGaeaThresholdNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Threshold;
	Descriptor.DisplayName = TEXT("Threshold");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Alters terrain or mask values around a height limit, with optional adaptive thresholding and inversion.");
	Descriptor.Inputs.Add(ThresholdAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(ThresholdAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ThresholdNumberParameter(TEXT("Scale"), TEXT("Scale"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(ThresholdNumberParameter(TEXT("Bias"), TEXT("Bias"), 0.0, -1.0, 1.0));
	Descriptor.Parameters.Add(ThresholdBooleanParameter(TEXT("Adaptive"), TEXT("Adaptive"), false));
	Descriptor.Parameters.Add(ThresholdBooleanParameter(TEXT("Invert"), TEXT("Invert"), false));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Threshold, EvaluateThresholdNode);
}
