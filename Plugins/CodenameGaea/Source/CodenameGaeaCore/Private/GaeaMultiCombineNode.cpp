#include "GaeaMultiCombineNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor MultiCombineAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor MultiCombineNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions = {
			TEXT("Blend"), TEXT("Add"), TEXT("Screen"), TEXT("Subtract"), TEXT("Multiply"),
			TEXT("Divide"), TEXT("Max"), TEXT("Min"), TEXT("SqRt"), TEXT("Power"), TEXT("Difference")
		};
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor MultiCombineNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = 0.0;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = 1.0;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor MultiCombineBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
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
		const FGaeaTerrainNode& Node,
		const TArray<const FGaeaTerrainValue*>& Values,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaScalarField& Base = Values[0]->ScalarField;
		if (!Base.IsValid())
		{
			Error = TEXT("MultiCombine Input must be a valid scalar field.");
			return false;
		}
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			if (!Values[Index]->ScalarField.IsValid() || Values[Index]->ScalarField.Domain != Base.Domain)
			{
				Error = TEXT("MultiCombine scalar inputs must use the same grid domain.");
				return false;
			}
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const bool bUniform = Node.GetBool(TEXT("Uniform"), true);
		const float UniformRatio = static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5));
		FGaeaScalarField Result = Base;
		Result.Descriptor.Name = TEXT("MultiCombine");
		for (int32 Layer = 1; Layer < Values.Num(); ++Layer)
		{
			const FString RatioNameString = FString::Printf(TEXT("Ratio%d"), Layer);
			const FName RatioName(*RatioNameString);
			const float Ratio = bUniform ? UniformRatio : static_cast<float>(Node.GetNumber(RatioName, 0.5));
			const FGaeaScalarField& LayerField = Values[Layer]->ScalarField;
			for (int32 Y = 0; Y < Result.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Result.Domain.Dimensions.X; ++X)
				{
					Result.AtInterior(X, Y) = FMath::Clamp(
						MultiCombineBlend(Result.AtInterior(X, Y), LayerField.AtInterior(X, Y), Method, Ratio),
						0.0f,
						1.0f);
				}
			}
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
		return true;
	}

	bool MultiCombineEvaluateTerrain(
		const FGaeaTerrainNode& Node,
		const TArray<const FGaeaTerrainValue*>& Values,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue& BaseValue = *Values[0];
		const FGaeaScalarField* BaseHeight = BaseValue.TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!BaseHeight || !BaseHeight->IsValid())
		{
			Error = TEXT("MultiCombine terrain Input requires a valid Height field.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const bool bUniform = Node.GetBool(TEXT("Uniform"), true);
		const float UniformRatio = static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5));
		const float BaseScale = FMath::Max(BaseValue.HeightScale, 1.0f);
		FGaeaScalarField ResultHeight = *BaseHeight;

		for (int32 Layer = 1; Layer < Values.Num(); ++Layer)
		{
			const FGaeaTerrainValue& LayerValue = *Values[Layer];
			const FGaeaScalarField* LayerHeight = LayerValue.TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!LayerHeight || !LayerHeight->IsValid() || LayerHeight->Domain != BaseHeight->Domain)
			{
				Error = TEXT("MultiCombine terrain inputs must contain matching Height domains.");
				return false;
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

		FGaeaTerrainDataset Dataset = BaseValue.TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("MultiCombine could not publish its Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), BaseScale));
		return true;
	}

	bool EvaluateMultiCombineNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* BasePtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Base = BasePtr ? *BasePtr : nullptr;
		if (!Base || !Base->IsValid())
		{
			Error = TEXT("MultiCombine requires a valid Input.");
			return false;
		}

		TArray<const FGaeaTerrainValue*> Values;
		Values.Add(Base);
		for (int32 Layer = 1; Layer <= 9; ++Layer)
		{
			const FName LayerName(*FString::Printf(TEXT("Layer%d"), Layer));
			const FGaeaTerrainValue* const* LayerPtr = Inputs.Find(LayerName);
			if (!LayerPtr || !*LayerPtr) continue;
			const FGaeaTerrainValue* LayerValue = *LayerPtr;
			if (!LayerValue->IsValid() || LayerValue->Type != Base->Type)
			{
				Error = FString::Printf(TEXT("MultiCombine %s must match Input value type."), *LayerName.ToString());
				return false;
			}
			Values.Add(LayerValue);
		}

		if (Base->Type == EGaeaTerrainValueType::ScalarField)
		{
			return MultiCombineEvaluateScalar(Node, Values, Out, Error);
		}
		if (Base->Type == EGaeaTerrainValueType::Terrain)
		{
			return MultiCombineEvaluateTerrain(Node, Values, Out, Error);
		}
		Error = TEXT("MultiCombine received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaMultiCombineNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::MultiCombine;
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
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::MultiCombine, EvaluateMultiCombineNode);
}
