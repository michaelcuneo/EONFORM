#include "EonformBlurNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor BlurAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor BlurNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	float BlurSampleClamped(const FEonformScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	bool BlurProcessField(const FEonformTerrainNode& Node, const FEonformScalarField& Source, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Blur received an invalid scalar field.");
			return false;
		}

		const float RadiusControl = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Radius"), 0.1)), 0.0f, 1.0f);
		if (RadiusControl <= UE_SMALL_NUMBER)
		{
			OutField = Source;
			return true;
		}

		const int32 MinDimension = FMath::Max(1, FMath::Min(Source.Domain.Dimensions.X, Source.Domain.Dimensions.Y));
		const int32 Radius = FMath::Clamp(FMath::RoundToInt(RadiusControl * static_cast<float>(MinDimension) * 0.02f), 1, 16);
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				float Sum = 0.0f;
				int32 Count = 0;
				for (int32 DY = -Radius; DY <= Radius; ++DY)
				{
					for (int32 DX = -Radius; DX <= Radius; ++DX)
					{
						Sum += BlurSampleClamped(Source, X + DX, Y + DY);
						++Count;
					}
				}
				OutField.AtInterior(X, Y) = Count > 0 ? Sum / static_cast<float>(Count) : Source.AtInterior(X, Y);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateBlurNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Blur requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			FEonformScalarField Result;
			if (!BlurProcessField(Node, Input->ScalarField, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Blur terrain input has no valid Height field.");
				return false;
			}

			FEonformScalarField ResultHeight;
			if (!BlurProcessField(Node, *Height, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Blur could not publish its Height field.");
				return false;
			}

			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Blur produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Blur received an unsupported input type.");
		return false;
	}
}

void RegisterEonformBlurNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Blur;
	Descriptor.DisplayName = TEXT("Blur");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Diffuses sharp shapes and softens terrain or mask data.");
	Descriptor.Inputs.Add(BlurAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(BlurAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(BlurNumberParameter(TEXT("Radius"), TEXT("Radius"), 0.1, 0.0, 1.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Blur, EvaluateBlurNode);
}
