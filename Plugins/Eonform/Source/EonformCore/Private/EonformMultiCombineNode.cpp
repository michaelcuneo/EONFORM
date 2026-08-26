#include "EonformMultiCombineNode.h"

#include "EonformTerrainDomainScaling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor MultiCombineAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor MultiCombineNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions = {
			TEXT("Blend"), TEXT("Add"), TEXT("Screen"), TEXT("Subtract"), TEXT("Multiply"),
			TEXT("Divide"), TEXT("Max"), TEXT("Min"), TEXT("SqRt"), TEXT("Power"), TEXT("Difference")
		};
		return Parameter;
	}

	FEonformTerrainParameterDescriptor MultiCombineNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = 0.0;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = 1.0;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor MultiCombineBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float MultiCombineApplyMethod(float A, float B, FName Method)
	{
		if (Method == TEXT("Add")) return A + B;
		if (Method == TEXT("Screen")) return 1.0f - (1.0f - A) * (1.0f - B);
		if (Method == TEXT("Subtract")) return A - B;
		if (Method == TEXT("Multiply")) return A * B;
		if (Method == TEXT("Divide")) return FMath::Abs(B) > UE_SMALL_NUMBER ? A / B : A;
		if (Method == TEXT("Max")) return FMath::Max(A, B);
		if (Method == TEXT("Min")) return FMath::Min(A, B);
		if (Method == TEXT("SqRt")) return FMath::Sqrt(FMath::Abs(A * B));
		if (Method == TEXT("Power"))
		{
			const float Magnitude = FMath::Pow(FMath::Max(FMath::Abs(A), UE_SMALL_NUMBER), B);
			return FMath::Sign(A) * Magnitude;
		}
		if (Method == TEXT("Difference")) return FMath::Abs(A - B);
		return B;
	}

	float MultiCombineBlend(float A, float B, FName Method, float Ratio)
	{
		const float T = FMath::Clamp(Ratio, 0.0f, 1.0f);
		if (Method == TEXT("Blend")) return FMath::Lerp(A, B, T);
		return FMath::Lerp(A, MultiCombineApplyMethod(A, B, Method), T);
	}

	bool MultiCombineEvaluateScalar(
		const FEonformTerrainNode& Node,
		const TArray<const FEonformTerrainValue*>& Values,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformScalarField& Base = Values[0]->ScalarField;
		if (!Base.IsValid())
		{
			Error = TEXT("MultiCombine Input must be a valid scalar field.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const bool bUniform = Node.GetBool(TEXT("Uniform"), true);
		const float UniformRatio = static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5));
		FEonformScalarField Result = Base;
		Result.Descriptor.Name = TEXT("MultiCombine");
		for (int32 Layer = 1; Layer < Values.Num(); ++Layer)
		{
			const FEonformScalarField& SourceLayer = Values[Layer]->ScalarField;
			if (!SourceLayer.IsValid())
			{
				Error = TEXT("MultiCombine layer must be a valid scalar field.");
				return false;
			}
			FEonformScalarField ScaledLayer;
			const FEonformScalarField* LayerField = &SourceLayer;
			if (SourceLayer.Domain != Base.Domain)
			{
				if (!EonformTerrainDomainScaling::ResampleScalarField(SourceLayer, Base.Domain, ScaledLayer, &Error)) return false;
				LayerField = &ScaledLayer;
			}
			const FString RatioNameString = FString::Printf(TEXT("Ratio%d"), Layer);
			const FName RatioName(*RatioNameString);
			const float Ratio = bUniform ? UniformRatio : static_cast<float>(Node.GetNumber(RatioName, 0.5));
			for (int32 Y = 0; Y < Result.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Result.Domain.Dimensions.X; ++X)
				{
					Result.AtInterior(X, Y) = FMath::Clamp(
						MultiCombineBlend(Result.AtInterior(X, Y), LayerField->AtInterior(X, Y), Method, Ratio),
						0.0f,
						1.0f);
				}
			}
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
		return true;
	}

	bool MultiCombineEvaluateTerrain(
		const FEonformTerrainNode& Node,
		const TArray<const FEonformTerrainValue*>& Values,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue& BaseValue = *Values[0];
		const FEonformScalarField* BaseHeight = BaseValue.TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!BaseHeight || !BaseHeight->IsValid())
		{
			Error = TEXT("MultiCombine terrain Input requires a valid Height field.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const bool bUniform = Node.GetBool(TEXT("Uniform"), true);
		const float UniformRatio = static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5));
		const float BaseScale = FMath::Max(BaseValue.HeightScale, 1.0f);
		FEonformScalarField ResultHeight = *BaseHeight;

		for (int32 Layer = 1; Layer < Values.Num(); ++Layer)
		{
			const FEonformTerrainValue& LayerValue = *Values[Layer];
			const FEonformScalarField* SourceHeight = LayerValue.TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!SourceHeight || !SourceHeight->IsValid())
			{
				Error = TEXT("MultiCombine terrain layers require valid Height fields.");
				return false;
			}
			FEonformScalarField ScaledHeight;
			const FEonformScalarField* LayerHeight = SourceHeight;
			if (SourceHeight->Domain != BaseHeight->Domain)
			{
				if (!EonformTerrainDomainScaling::ResampleScalarField(*SourceHeight, BaseHeight->Domain, ScaledHeight, &Error)) return false;
				LayerHeight = &ScaledHeight;
			}
			const FString RatioNameString = FString::Printf(TEXT("Ratio%d"), Layer);
			const FName RatioName(*RatioNameString);
			const float Ratio = bUniform ? UniformRatio : static_cast<float>(Node.GetNumber(RatioName, 0.5));
			const float LayerScaleFactor = FMath::Max(LayerValue.HeightScale, 1.0f) / BaseScale;
			for (int32 Y = 0; Y < ResultHeight.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < ResultHeight.Domain.Dimensions.X; ++X)
				{
					const float B = LayerHeight->AtInterior(X, Y) * LayerScaleFactor;
					ResultHeight.AtInterior(X, Y) = FMath::Clamp(
						MultiCombineBlend(ResultHeight.AtInterior(X, Y), B, Method, Ratio),
						-1.0f,
						1.0f);
				}
			}
		}

		FEonformTerrainDataset Dataset = BaseValue.TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("MultiCombine could not publish its Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), BaseScale));
		return true;
	}

	bool EvaluateMultiCombineNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* BasePtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Base = BasePtr ? *BasePtr : nullptr;
		if (!Base || !Base->IsValid())
		{
			Error = TEXT("MultiCombine requires a valid Input.");
			return false;
		}

		TArray<const FEonformTerrainValue*> Values;
		Values.Add(Base);
		for (int32 Layer = 1; Layer <= 9; ++Layer)
		{
			const FName LayerName(*FString::Printf(TEXT("Layer%d"), Layer));
			const FEonformTerrainValue* const* LayerPtr = Inputs.Find(LayerName);
			if (!LayerPtr || !*LayerPtr) continue;
			const FEonformTerrainValue* LayerValue = *LayerPtr;
			if (!LayerValue->IsValid() || LayerValue->Type != Base->Type)
			{
				Error = FString::Printf(TEXT("MultiCombine %s must match Input value type."), *LayerName.ToString());
				return false;
			}
			Values.Add(LayerValue);
		}

		if (Base->Type == EEonformTerrainValueType::ScalarField)
		{
			return MultiCombineEvaluateScalar(Node, Values, Out, Error);
		}
		if (Base->Type == EEonformTerrainValueType::Terrain)
		{
			return MultiCombineEvaluateTerrain(Node, Values, Out, Error);
		}
		Error = TEXT("MultiCombine received an unsupported input type.");
		return false;
	}
}

void RegisterEonformMultiCombineNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::MultiCombine;
	Descriptor.DisplayName = TEXT("MultiCombine");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Combines an Input with up to nine additional layers using a shared blend method and uniform or per-layer ratios.");
	Descriptor.Inputs.Add(MultiCombineAnyPort(TEXT("Input"), TEXT("Input")));
	for (int32 Layer = 1; Layer <= 9; ++Layer)
	{
		const FString LayerNameString = FString::Printf(TEXT("Layer%d"), Layer);
		const FString LayerDisplay = FString::Printf(TEXT("Layer %d"), Layer);
		Descriptor.Inputs.Add(MultiCombineAnyPort(FName(*LayerNameString), *LayerDisplay));
	}
	Descriptor.Outputs.Add(MultiCombineAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(MultiCombineNameParameter(TEXT("Method"), TEXT("Method"), TEXT("Blend")));
	Descriptor.Parameters.Add(MultiCombineBooleanParameter(TEXT("Uniform"), TEXT("Uniform"), true));
	Descriptor.Parameters.Add(MultiCombineNumberParameter(TEXT("Ratio"), TEXT("Ratio"), 0.5));
	for (int32 Layer = 1; Layer <= 9; ++Layer)
	{
		const FString RatioNameString = FString::Printf(TEXT("Ratio%d"), Layer);
		const FString RatioDisplay = FString::Printf(TEXT("Ratio %d"), Layer);
		Descriptor.Parameters.Add(MultiCombineNumberParameter(FName(*RatioNameString), *RatioDisplay, 0.5));
	}
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::MultiCombine, EvaluateMultiCombineNode);
}
