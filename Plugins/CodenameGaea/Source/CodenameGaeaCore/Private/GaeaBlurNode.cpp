#include "GaeaBlurNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor BlurAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor BlurNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	float BlurSampleClamped(const FGaeaScalarField& Field, int32 X, int32 Y)
	{
		return Field.AtInterior(
			FMath::Clamp(X, 0, Field.Domain.Dimensions.X - 1),
			FMath::Clamp(Y, 0, Field.Domain.Dimensions.Y - 1));
	}

	bool BlurProcessField(const FGaeaTerrainNode& Node, const FGaeaScalarField& Source, FGaeaScalarField& OutField, FString& Error)
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

	bool EvaluateBlurNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Blur requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!BlurProcessField(Node, Input->ScalarField, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Blur terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField ResultHeight;
			if (!BlurProcessField(Node, *Height, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Blur could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
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

void RegisterGaeaBlurNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Blur;
	Descriptor.DisplayName = TEXT("Blur");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Diffuses sharp shapes and softens terrain or mask data.");
	Descriptor.Inputs.Add(BlurAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(BlurAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(BlurNumberParameter(TEXT("Radius"), TEXT("Radius"), 0.1, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Blur, EvaluateBlurNode);
}
