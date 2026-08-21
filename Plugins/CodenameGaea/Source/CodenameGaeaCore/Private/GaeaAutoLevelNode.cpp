#include "GaeaAutoLevelNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor AutoLevelAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	bool AutoLevelProcessField(const FGaeaScalarField& Source, bool bTerrain, FGaeaScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("Autolevel received an invalid scalar field.");
			return false;
		}

		float SourceMin = TNumericLimits<float>::Max();
		float SourceMax = TNumericLimits<float>::Lowest();
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Value = Source.AtInterior(X, Y);
				SourceMin = FMath::Min(SourceMin, Value);
				SourceMax = FMath::Max(SourceMax, Value);
			}
		}

		const float SourceRange = SourceMax - SourceMin;
		if (SourceRange <= UE_SMALL_NUMBER)
		{
			OutField = Source;
			return true;
		}

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Normalized = FMath::Clamp((Source.AtInterior(X, Y) - SourceMin) / SourceRange, 0.0f, 1.0f);
				OutField.AtInterior(X, Y) = bTerrain ? Normalized * 2.0f - 1.0f : Normalized;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateAutoLevelNode(const FGaeaTerrainNode&, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Autolevel requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!AutoLevelProcessField(Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Autolevel terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField ResultHeight;
			if (!AutoLevelProcessField(*Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Autolevel could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("Autolevel produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("Autolevel received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaAutoLevelNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::AutoLevel;
	Descriptor.DisplayName = TEXT("Autolevel");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Automatically remaps the input to use the full available terrain or mask range.");
	Descriptor.Inputs.Add(AutoLevelAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(AutoLevelAnyPort(TEXT("Out"), TEXT("Out")));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::AutoLevel, EvaluateAutoLevelNode);
}
