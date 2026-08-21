#include "GaeaInvertNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor InvertAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	bool InvertScalarField(const FGaeaScalarField& Source, bool bTerrainHeight, FGaeaScalarField& OutField)
	{
		if (!Source.IsValid()) return false;
		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Value = Source.AtInterior(X, Y);
				OutField.AtInterior(X, Y) = bTerrainHeight ? -Value : 1.0f - FMath::Clamp(Value, 0.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateInvertNode(
		const FGaeaTerrainNode&,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Invert requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!InvertScalarField(Input->ScalarField, false, Result))
			{
				Error = TEXT("Invert could not process the scalar field input.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Invert terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField InvertedHeight;
			if (!InvertScalarField(*Height, true, InvertedHeight))
			{
				Error = TEXT("Invert could not process the terrain Height field.");
				return false;
			}
			InvertedHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(InvertedHeight)))
			{
				Error = TEXT("Invert could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Invert produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Invert received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaInvertNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Invert;
	Descriptor.DisplayName = TEXT("Invert");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Inverts terrain height or scalar mask values.");
	Descriptor.Inputs.Add(InvertAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(InvertAnyPort(TEXT("Out"), TEXT("Out")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Invert, EvaluateInvertNode);
}
