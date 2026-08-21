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

	float CombineApplyMethod(float A, float B, FName Method)
	{
		if (Method == TEXT("Add")) return A + B;
		if (Method == TEXT("Subtract")) return A - B;
		if (Method == TEXT("Multiply")) return A * B;
		if (Method == TEXT("Divide")) return FMath::Abs(B) > UE_SMALL_NUMBER ? A / B : A;
		if (Method == TEXT("Max")) return FMath::Max(A, B);
		if (Method == TEXT("Min")) return FMath::Min(A, B);
		if (Method == TEXT("Difference")) return FMath::Abs(A - B);
		if (Method == TEXT("Screen")) return 1.0f - (1.0f - A) * (1.0f - B);
		return B;
	}

	bool CombineEvaluateScalar(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainValue& Primary,
		const FGaeaTerrainValue& Secondary,
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

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		FGaeaScalarField Result = Primary.ScalarField;
		Result.Descriptor.Name = TEXT("Combine");
		Result.Descriptor.Unit = EGaeaFieldUnit::Normalized;

		const FIntPoint Dimensions = Result.Domain.Dimensions;
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const float A = Primary.ScalarField.AtInterior(X, Y);
				const float B = Secondary.ScalarField.AtInterior(X, Y);
				const float Candidate = CombineApplyMethod(A, B, Method);
				const float Value = Method == TEXT("Blend")
					? FMath::Lerp(A, B, Ratio)
					: FMath::Lerp(A, Candidate, Ratio);
				Result.AtInterior(X, Y) = FMath::Clamp(Value, 0.0f, 1.0f);
			}
		}

		if (!Result.IsValid())
		{
			Error = TEXT("Combine produced an invalid scalar field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
		return true;
	}

	bool CombineEvaluateTerrain(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainValue& Primary,
		const FGaeaTerrainValue& Secondary,
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

		const FName Method = Node.GetName(TEXT("Method"), TEXT("Blend"));
		const float Ratio = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Ratio"), 0.5)), 0.0f, 1.0f);
		const float PrimaryScale = FMath::Max(Primary.HeightScale, 1.0f);
		const float SecondaryScale = FMath::Max(Secondary.HeightScale, 1.0f);
		const float SecondaryToPrimaryScale = SecondaryScale / PrimaryScale;

		FGaeaScalarField ResultHeight = *PrimaryHeight;
		const FIntPoint Dimensions = ResultHeight.Domain.Dimensions;
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const float A = PrimaryHeight->AtInterior(X, Y);
				const float B = SecondaryHeight->AtInterior(X, Y) * SecondaryToPrimaryScale;
				const float Candidate = CombineApplyMethod(A, B, Method);
				const float Value = Method == TEXT("Blend")
					? FMath::Lerp(A, B, Ratio)
					: FMath::Lerp(A, Candidate, Ratio);
				ResultHeight.AtInterior(X, Y) = FMath::Clamp(Value, -1.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset ResultDataset = Primary.TerrainDataset;
		if (!ResultDataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Combine could not publish its terrain Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(ResultDataset), PrimaryScale));
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
		if (Primary->Type != Secondary->Type)
		{
			Error = TEXT("Combine inputs must be the same value type.");
			return false;
		}

		if (Primary->Type == EGaeaTerrainValueType::ScalarField)
		{
			return CombineEvaluateScalar(Node, *Primary, *Secondary, Out, Error);
		}
		if (Primary->Type == EGaeaTerrainValueType::Terrain)
		{
			return CombineEvaluateTerrain(Node, *Primary, *Secondary, Out, Error);
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
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Combines two terrains or two masks using the selected method and ratio.");
	Descriptor.Inputs.Add(CombineAnyPort(TEXT("Primary"), TEXT("Primary")));
	Descriptor.Inputs.Add(CombineAnyPort(TEXT("Secondary"), TEXT("Secondary")));
	Descriptor.Outputs.Add(CombineAnyPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(CombineNameParameter(
		TEXT("Method"),
		TEXT("Method"),
		TEXT("Blend"),
		{ TEXT("Blend"), TEXT("Add"), TEXT("Subtract"), TEXT("Multiply"), TEXT("Divide"), TEXT("Max"), TEXT("Min"), TEXT("Difference"), TEXT("Screen") }));
	Descriptor.Parameters.Add(CombineNumberParameter(TEXT("Ratio"), TEXT("Ratio"), 0.5, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Combine, EvaluateCombineNode);
}
