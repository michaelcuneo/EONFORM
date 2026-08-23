#include "GaeaCombineNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor CombineAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor CombineScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor CombineNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor CombineNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	float CombineTo01(float V) { return FMath::Clamp(V, 0.0f, 1.0f); }
	float CombineFrom01(float V) { return FMath::Clamp(V, 0.0f, 1.0f); }

	float CombineMode01(float A, float B, FName Mode)
	{
		if (Mode == TEXT("Blend")) return B;
		if (Mode == TEXT("Add")) return A + B;
		if (Mode == TEXT("Screen")) return 1.0f - (1.0f - A) * (1.0f - B);
		if (Mode == TEXT("Subtract")) return A - B;
		if (Mode == TEXT("Difference")) return FMath::Abs(A - B);
		if (Mode == TEXT("Multiply")) return A * B;
		if (Mode == TEXT("Divide")) return FMath::Abs(B) > UE_SMALL_NUMBER ? A / B : A;
		if (Mode == TEXT("Divide 2")) return FMath::Abs(A) > UE_SMALL_NUMBER ? B / A : B;
		if (Mode == TEXT("Max")) return FMath::Max(A, B);
		if (Mode == TEXT("Min")) return FMath::Min(A, B);
		if (Mode == TEXT("Hypotenuse")) return FMath::Sqrt(A * A + B * B);
		if (Mode == TEXT("Overlay")) return A < 0.5f ? 2.0f * A * B : 1.0f - 2.0f * (1.0f - A) * (1.0f - B);
		if (Mode == TEXT("Power")) return FMath::Pow(FMath::Max(A, UE_SMALL_NUMBER), B);
		if (Mode == TEXT("Exclusion")) return A + B - 2.0f * A * B;
		if (Mode == TEXT("Dodge")) return B >= 1.0f ? 1.0f : A / FMath::Max(1.0f - B, UE_SMALL_NUMBER);
		if (Mode == TEXT("Burn")) return B <= 0.0f ? 0.0f : 1.0f - (1.0f - A) / FMath::Max(B, UE_SMALL_NUMBER);
		if (Mode == TEXT("Soft Light")) return (1.0f - 2.0f * B) * A * A + 2.0f * B * A;
		if (Mode == TEXT("Hard Light")) return B < 0.5f ? 2.0f * A * B : 1.0f - 2.0f * (1.0f - A) * (1.0f - B);
		if (Mode == TEXT("Pin Light")) return B < 0.5f ? FMath::Min(A, 2.0f * B) : FMath::Max(A, 2.0f * B - 1.0f);
		if (Mode == TEXT("Grain Merge")) return A + B - 0.5f;
		if (Mode == TEXT("Grain Extract")) return A - B + 0.5f;
		if (Mode == TEXT("Reflect")) return B >= 1.0f ? 1.0f : A * A / FMath::Max(1.0f - B, UE_SMALL_NUMBER);
		if (Mode == TEXT("Glow")) return A >= 1.0f ? 1.0f : B * B / FMath::Max(1.0f - A, UE_SMALL_NUMBER);
		if (Mode == TEXT("Phoenix")) return FMath::Min(A, B) - FMath::Max(A, B) + 1.0f;
		return B;
	}

	float CombineEnhance(float V, FName Enhance)
	{
		if (Enhance == TEXT("Equalize")) return V * V * (3.0f - 2.0f * V);
		return V;
	}

	float CombineOutput(float V, FName Output)
	{
		if (Output == TEXT("Clamp")) return FMath::Clamp(V, 0.0f, 1.0f);
		if (Output == TEXT("Extend")) return 0.5f + 0.5f * FMath::Tanh((V - 0.5f) * 2.0f);
		return V;
	}

	bool CombineProcess(const FGaeaTerrainNode& Node, const FGaeaScalarField& AField, const FGaeaScalarField& BField, const FGaeaScalarField* Mask, bool bTerrain, FGaeaScalarField& OutField, FString& Error)
	{
		if (!AField.IsValid() || !BField.IsValid() || AField.Domain != BField.Domain)
		{
			Error = TEXT("Combine inputs must be valid and use the same grid domain.");
			return false;
		}
		if (Mask && (!Mask->IsValid() || Mask->Domain != AField.Domain))
		{
			Error = TEXT("Combine Mask must use the same grid domain as its inputs.");
			return false;
		}

		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Blend"));
		const FName Output = Node.GetName(TEXT("Output"), TEXT("None"));
		const FName Enhance = Node.GetName(TEXT("Enhance"), TEXT("None"));
		OutField = AField;
		for (int32 Y = 0; Y < AField.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < AField.Domain.Dimensions.X; ++X)
			{
				const float RawA = AField.AtInterior(X, Y);
				const float RawB = BField.AtInterior(X, Y);
				const float A = bTerrain ? CombineTo01(RawA) : FMath::Clamp(RawA, 0.0f, 1.0f);
				const float B = bTerrain ? CombineTo01(RawB) : FMath::Clamp(RawB, 0.0f, 1.0f);
				float Combined = FMath::Lerp(A, CombineMode01(A, B, Mode), Ratio);
				Combined = CombineOutput(Combined, Output);
				Combined = CombineEnhance(Combined, Enhance);
				if (Mask) Combined = FMath::Lerp(A, Combined, FMath::Clamp(Mask->AtInterior(X, Y), 0.0f, 1.0f));
				OutField.AtInterior(X, Y) = bTerrain ? CombineFrom01(Combined) : FMath::Clamp(Combined, 0.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateCombineNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* Input1Ptr = Inputs.Find(TEXT("Input1"));
		const FGaeaTerrainValue* const* Input2Ptr = Inputs.Find(TEXT("Input2"));
		const FGaeaTerrainValue* Input1 = Input1Ptr ? *Input1Ptr : nullptr;
		const FGaeaTerrainValue* Input2 = Input2Ptr ? *Input2Ptr : nullptr;
		if (!Input1 || !Input2 || !Input1->IsValid() || !Input2->IsValid() || Input1->Type != Input2->Type)
		{
			Error = TEXT("Combine requires valid Input1 and Input2 values of the same type.");
			return false;
		}

		const FGaeaTerrainValue* const* MaskPtr = Inputs.Find(TEXT("Mask"));
		const FGaeaTerrainValue* MaskValue = MaskPtr ? *MaskPtr : nullptr;
		const FGaeaScalarField* Mask = nullptr;
		if (MaskValue)
		{
			if (MaskValue->Type != EGaeaTerrainValueType::ScalarField || !MaskValue->ScalarField.IsValid())
			{
				Error = TEXT("Combine Mask must be a scalar field.");
				return false;
			}
			Mask = &MaskValue->ScalarField;
		}

		if (Input1->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!CombineProcess(Node, Input1->ScalarField, Input2->ScalarField, Mask, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input1->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* A = Input1->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			const FGaeaScalarField* B = Input2->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!A || !B)
			{
				Error = TEXT("Combine terrain inputs require Height fields.");
				return false;
			}
			FGaeaScalarField ResultHeight;
			if (!CombineProcess(Node, *A, *B, Mask, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;
			FGaeaTerrainDataset Dataset = Input1->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight))) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input1->HeightScale));
			return true;
		}
		Error = TEXT("Combine received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaCombineNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Combine;
	Descriptor.DisplayName = TEXT("Combine");
	Descriptor.Category = TEXT("Utility");
	Descriptor.Description = TEXT("Combines two terrain or mask inputs using Gaea's current blend modes, ratio, output handling, enhancement, and optional mask.");
	Descriptor.Inputs.Add(CombineAnyPort(TEXT("Input1"), TEXT("Input1")));
	Descriptor.Inputs.Add(CombineAnyPort(TEXT("Input2"), TEXT("Input2")));
	Descriptor.Inputs.Add(CombineScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Outputs.Add(CombineAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(CombineNumberParameter(TEXT("Ratio"), TEXT("Ratio"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(CombineNameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Blend"), { TEXT("Blend"), TEXT("Add"), TEXT("Screen"), TEXT("Subtract"), TEXT("Difference"), TEXT("Multiply"), TEXT("Divide"), TEXT("Divide 2"), TEXT("Max"), TEXT("Min"), TEXT("Hypotenuse"), TEXT("Overlay"), TEXT("Power"), TEXT("Exclusion"), TEXT("Dodge"), TEXT("Burn"), TEXT("Soft Light"), TEXT("Hard Light"), TEXT("Pin Light"), TEXT("Grain Merge"), TEXT("Grain Extract"), TEXT("Reflect"), TEXT("Glow"), TEXT("Phoenix") }));
	Descriptor.Parameters.Add(CombineNameParameter(TEXT("Output"), TEXT("Output"), TEXT("None"), { TEXT("None"), TEXT("Clamp"), TEXT("Extend") }));
	Descriptor.Parameters.Add(CombineNameParameter(TEXT("Enhance"), TEXT("Enhance"), TEXT("None"), { TEXT("None"), TEXT("Autolevel"), TEXT("Equalize") }));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Combine, EvaluateCombineNode);
}
