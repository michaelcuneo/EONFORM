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

	FGaeaTerrainParameterDescriptor CombineNameParameter(
		FName Name,
		const TCHAR* DisplayName,
		FName DefaultValue,
		std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor CombineNumberParameter(
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

	FGaeaTerrainParameterDescriptor CombineBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float CombineApplyMethod(float A, float B, FName Method, float Threshold, float Flatten)
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
		if (Method == TEXT("Insert"))
		{
			const float InsertWeight = FMath::Clamp((B - Threshold) / FMath::Max(1.0f - Threshold, UE_SMALL_NUMBER), 0.0f, 1.0f);
			const float FlattenedBase = FMath::Lerp(A, FMath::Min(A, B), FMath::Clamp(Flatten, 0.0f, 1.0f));
			return FMath::Lerp(FlattenedBase, B, InsertWeight);
		}
		if (Method == TEXT("Embed"))
		{
			const float EmbedWeight = FMath::Clamp((B - Threshold) / FMath::Max(1.0f - Threshold, UE_SMALL_NUMBER), 0.0f, 1.0f);
			return A + B * EmbedWeight;
		}
		return B;
	}

	float CombineFinalizeValue(float A, float B, float Candidate, FName Method, float Ratio, float MaskValue, bool bClampOutput, bool bAbs, float Threshold, bool bTerrain)
	{
		float Value = Method == TEXT("Blend")
			? FMath::Lerp(A, B, Ratio)
			: FMath::Lerp(A, Candidate, Ratio);

		Value = FMath::Lerp(B, Value, FMath::Clamp(MaskValue, 0.0f, 1.0f));
		if (bAbs)
		{
			Value = Value > Threshold ? 1.0f : 0.0f;
		}
		if (bClampOutput)
		{
			Value = bTerrain ? FMath::Clamp(Value, -1.0f, 1.0f) : FMath::Clamp(Value, 0.0f, 1.0f);
		}
		return Value;
	}

	bool CombineEvaluateScalar(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainValue& Primary,
		const FGaeaTerrainValue& Secondary,
		const FGaeaScalarField* AreaMask,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		if (!Primary.ScalarField.IsValid() || !Secondary.ScalarField.IsValid())
		{
			Error = TEXT("Combine requires valid scalar fields.");
			return false;
		}
		if (Primary.ScalarField.Domain != Secondary.ScalarField.Domain)
		{
			Error = TEXT("Combine scalar inputs must use the same grid domain.");
			return false;
		}
		if (AreaMask && AreaMask->Domain != Primary.ScalarField.Domain)
		{
			Error = TEXT("Combine Mask must use the same grid domain as its inputs.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		const bool bClampOutput = Node.GetBool(TEXT("ClampOutput"), true);
		const bool bAbs = Node.GetBool(TEXT("Abs"), false);
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.5)), 0.0f, 1.0f);
		const float Flatten = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Flatten"), 0.0)), 0.0f, 1.0f);
		const bool bSeparationMask = Node.GetBool(TEXT("SeparationMask"), false);

		FGaeaScalarField Result = Primary.ScalarField;
		Result.Descriptor.Name = TEXT("Combine");
		Result.Descriptor.Unit = EGaeaFieldUnit::Normalized;
		FGaeaScalarField Separation = Primary.ScalarField;
		Separation.Descriptor.Name = TEXT("Separation");
		Separation.Descriptor.Unit = EGaeaFieldUnit::Normalized;

		const FIntPoint Dimensions = Result.Domain.Dimensions;
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const float A = Primary.ScalarField.AtInterior(X, Y);
				const float B = Secondary.ScalarField.AtInterior(X, Y);
				const float Candidate = CombineApplyMethod(A, B, Method, Threshold, Flatten);
				const float MaskValue = AreaMask ? AreaMask->AtInterior(X, Y) : 1.0f;
				Result.AtInterior(X, Y) = CombineFinalizeValue(A, B, Candidate, Method, Ratio, MaskValue, bClampOutput, bAbs, Threshold, false);
				Separation.AtInterior(X, Y) = bSeparationMask ? FMath::Clamp(FMath::Abs(A - B), 0.0f, 1.0f) : 0.0f;
			}
		}

		if (!Result.IsValid() || !Separation.IsValid())
		{
			Error = TEXT("Combine produced an invalid scalar result.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
		Out.Outputs.Add(TEXT("Separation"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Separation)));
		return true;
	}

	bool CombineEvaluateTerrain(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainValue& Primary,
		const FGaeaTerrainValue& Secondary,
		const FGaeaScalarField* AreaMask,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaScalarField* PrimaryHeight = Primary.TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* SecondaryHeight = Secondary.TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!PrimaryHeight || !PrimaryHeight->IsValid() || !SecondaryHeight || !SecondaryHeight->IsValid())
		{
			Error = TEXT("Combine terrain inputs require valid Height fields.");
			return false;
		}
		if (PrimaryHeight->Domain != SecondaryHeight->Domain)
		{
			Error = TEXT("Combine terrain inputs must use the same grid domain.");
			return false;
		}
		if (AreaMask && AreaMask->Domain != PrimaryHeight->Domain)
		{
			Error = TEXT("Combine Mask must use the same grid domain as its terrain inputs.");
			return false;
		}

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		const bool bClampOutput = Node.GetBool(TEXT("ClampOutput"), true);
		const bool bAbs = Node.GetBool(TEXT("Abs"), false);
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.5)), 0.0f, 1.0f);
		const float Flatten = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Flatten"), 0.0)), 0.0f, 1.0f);
		const bool bSeparationMask = Node.GetBool(TEXT("SeparationMask"), false);
		const float PrimaryScale = FMath::Max(Primary.HeightScale, 1.0f);
		const float SecondaryScale = FMath::Max(Secondary.HeightScale, 1.0f);
		const float SecondaryToPrimaryScale = SecondaryScale / PrimaryScale;

		FGaeaScalarField ResultHeight = *PrimaryHeight;
		FGaeaScalarField Separation = *PrimaryHeight;
		Separation.Descriptor.Name = TEXT("Separation");
		Separation.Descriptor.Unit = EGaeaFieldUnit::Normalized;
		const FIntPoint Dimensions = ResultHeight.Domain.Dimensions;
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const float A = PrimaryHeight->AtInterior(X, Y);
				const float B = SecondaryHeight->AtInterior(X, Y) * SecondaryToPrimaryScale;
				const float Candidate = CombineApplyMethod(A, B, Method, Threshold, Flatten);
				const float MaskValue = AreaMask ? AreaMask->AtInterior(X, Y) : 1.0f;
				ResultHeight.AtInterior(X, Y) = CombineFinalizeValue(A, B, Candidate, Method, Ratio, MaskValue, bClampOutput, bAbs, Threshold, true);
				Separation.AtInterior(X, Y) = bSeparationMask ? FMath::Clamp(FMath::Abs(A - B), 0.0f, 1.0f) : 0.0f;
			}
		}

		FGaeaTerrainDataset ResultDataset = Primary.TerrainDataset;
		if (!ResultDataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Combine could not publish its terrain Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(ResultDataset), PrimaryScale));
		Out.Outputs.Add(TEXT("Separation"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Separation)));
		return true;
	}

	bool EvaluateCombineNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* PrimaryPtr = Inputs.Find(TEXT("Primary"));
		const FGaeaTerrainValue* const* SecondaryPtr = Inputs.Find(TEXT("Secondary"));
		const FGaeaTerrainValue* Primary = PrimaryPtr ? *PrimaryPtr : nullptr;
		const FGaeaTerrainValue* Secondary = SecondaryPtr ? *SecondaryPtr : nullptr;
		if (!Primary || !Secondary || !Primary->IsValid() || !Secondary->IsValid())
		{
			Error = TEXT("Combine requires valid Primary and Secondary inputs.");
			return false;
		}

		if (Node.GetBool(TEXT("SwapInputs"), false))
		{
			Swap(Primary, Secondary);
		}
		if (Primary->Type != Secondary->Type)
		{
			Error = TEXT("Combine currently requires Primary and Secondary to be the same graph value type.");
			return false;
		}

		const FGaeaTerrainValue* const* MaskPtr = Inputs.Find(TEXT("Mask"));
		const FGaeaTerrainValue* MaskValue = MaskPtr ? *MaskPtr : nullptr;
		const FGaeaScalarField* AreaMask = nullptr;
		if (MaskValue)
		{
			if (MaskValue->Type != EGaeaTerrainValueType::ScalarField || !MaskValue->ScalarField.IsValid())
			{
				Error = TEXT("Combine Mask input must be a valid scalar field.");
				return false;
			}
			AreaMask = &MaskValue->ScalarField;
		}

		if (Primary->Type == EGaeaTerrainValueType::ScalarField)
		{
			return CombineEvaluateScalar(Node, *Primary, *Secondary, AreaMask, Out, Error);
		}
		if (Primary->Type == EGaeaTerrainValueType::Terrain)
		{
			return CombineEvaluateTerrain(Node, *Primary, *Secondary, AreaMask, Out, Error);
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
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Combines two terrain or mask outputs using Gaea-style blend methods, ratio, masking, and separation output.");
	Descriptor.Inputs.Add(CombineAnyPort(TEXT("Primary"), TEXT("Input1")));
	Descriptor.Inputs.Add(CombineAnyPort(TEXT("Secondary"), TEXT("Input2")));
	Descriptor.Inputs.Add(CombineScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Outputs.Add(CombineAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(CombineScalarPort(TEXT("Separation"), TEXT("Separation")));
	Descriptor.Parameters.Add(CombineNameParameter(
		TEXT("Method"),
		TEXT("Method"),
		TEXT("Blend"),
		{ TEXT("Blend"), TEXT("Add"), TEXT("Screen"), TEXT("Subtract"), TEXT("Multiply"), TEXT("Divide"), TEXT("Max"), TEXT("Min"), TEXT("SqRt"), TEXT("Power"), TEXT("Difference"), TEXT("Insert"), TEXT("Embed") }));
	Descriptor.Parameters.Add(CombineNumberParameter(TEXT("Ratio"), TEXT("Ratio"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(CombineBooleanParameter(TEXT("SwapInputs"), TEXT("Swap Inputs"), false));
	Descriptor.Parameters.Add(CombineBooleanParameter(TEXT("SeparationMask"), TEXT("Separation Mask"), false));
	Descriptor.Parameters.Add(CombineBooleanParameter(TEXT("ClampOutput"), TEXT("Clamp Output"), true));
	Descriptor.Parameters.Add(CombineBooleanParameter(TEXT("Abs"), TEXT("Abs"), false));
	Descriptor.Parameters.Add(CombineNumberParameter(TEXT("Threshold"), TEXT("Threshold"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(CombineNumberParameter(TEXT("Flatten"), TEXT("Flatten"), 0.0, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Combine, EvaluateCombineNode);
}
