#include "EonformAutoLevelNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainGlobalSummary.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformAutoLevelNode
{
	bool ApplyRange(
		const FEonformScalarField& Source,
		bool bTerrain,
		float SourceMinimum,
		float SourceMaximum,
		FEonformScalarField& OutField,
		FString* OutError)
	{
		if (!Source.IsValid())
		{
			if (OutError) *OutError = TEXT("Autolevel received an invalid scalar field.");
			return false;
		}
		if (!FMath::IsFinite(SourceMinimum) || !FMath::IsFinite(SourceMaximum) || SourceMaximum < SourceMinimum)
		{
			if (OutError) *OutError = TEXT("Autolevel received an invalid source range.");
			return false;
		}

		const float SourceRange = SourceMaximum - SourceMinimum;
		if (SourceRange <= UE_SMALL_NUMBER)
		{
			OutField = Source;
			if (OutError) OutError->Reset();
			return true;
		}

		OutField = Source;
		const FIntPoint Storage = Source.Domain.GetStorageDimensions();
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const float Normalized = FMath::Clamp(
					(Source.AtStorage(X, Y) - SourceMinimum) / SourceRange,
					0.0f,
					1.0f);
				OutField.AtStorage(X, Y) = bTerrain ? Normalized * 2.0f - 1.0f : Normalized;
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}
}

namespace
{
	FEonformTerrainPortDescriptor AutoLevelAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	bool ResolveLocalRange(const FEonformScalarField& Source, float& OutMinimum, float& OutMaximum)
	{
		if (!Source.IsValid()) return false;
		OutMinimum = TNumericLimits<float>::Max();
		OutMaximum = TNumericLimits<float>::Lowest();
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Value = Source.AtInterior(X, Y);
				OutMinimum = FMath::Min(OutMinimum, Value);
				OutMaximum = FMath::Max(OutMaximum, Value);
			}
		}
		return FMath::IsFinite(OutMinimum) && FMath::IsFinite(OutMaximum);
	}

	const FEonformTerrainConnection* ResolveInputConnection(
		const FEonformTerrainRecipe& Recipe,
		const FGuid& NodeId)
	{
		const FEonformTerrainConnection* Match = nullptr;
		for (const FEonformTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode != NodeId || Connection.ToInput != TEXT("Input")) continue;
			if (Match) return nullptr;
			Match = &Connection;
		}
		return Match;
	}

	bool ResolveAutoLevelRange(
		const FEonformTerrainNode& Node,
		const FEonformScalarField& Source,
		const FEonformTerrainEvaluationContext& Context,
		float& OutMinimum,
		float& OutMaximum,
		FString& Error)
	{
		if (!Context.HasRegion())
		{
			if (!ResolveLocalRange(Source, OutMinimum, OutMaximum))
			{
				Error = TEXT("Autolevel could not resolve its source range.");
				return false;
			}
			return true;
		}

		if (!Context.ActiveRecipe)
		{
			Error = TEXT("Regional Autolevel requires an active graph for whole-world summary evaluation.");
			return false;
		}
		const FEonformTerrainConnection* Connection = ResolveInputConnection(*Context.ActiveRecipe, Node.Id);
		if (!Connection)
		{
			Error = TEXT("Regional Autolevel requires exactly one connected Input.");
			return false;
		}
		if (!FEonformTerrainGlobalSummary::ResolveOutputRange(
			*Context.ActiveRecipe,
			Context,
			Connection->FromNode,
			Connection->FromOutput,
			OutMinimum,
			OutMaximum,
			&Error))
		{
			return false;
		}
		return true;
	}

	bool EvaluateAutoLevelNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("Autolevel requires a valid Input.");
			return false;
		}

		if (Input->Type == EEonformTerrainValueType::ScalarField)
		{
			float SourceMinimum = 0.0f;
			float SourceMaximum = 0.0f;
			if (!ResolveAutoLevelRange(Node, Input->ScalarField, Context, SourceMinimum, SourceMaximum, Error)) return false;

			FEonformScalarField Result;
			if (!EonformAutoLevelNode::ApplyRange(
				Input->ScalarField,
				false,
				SourceMinimum,
				SourceMaximum,
				Result,
				&Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Autolevel terrain input has no valid Height field.");
				return false;
			}

			float SourceMinimum = 0.0f;
			float SourceMaximum = 0.0f;
			if (!ResolveAutoLevelRange(Node, *Height, Context, SourceMinimum, SourceMaximum, Error)) return false;

			FEonformScalarField ResultHeight;
			if (!EonformAutoLevelNode::ApplyRange(
				*Height,
				true,
				SourceMinimum,
				SourceMaximum,
				ResultHeight,
				&Error)) return false;
			ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;

			FEonformTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("Autolevel could not publish its Height field.");
				return false;
			}

			FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
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

void RegisterEonformAutoLevelNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::AutoLevel;
	Descriptor.DisplayName = TEXT("Autolevel");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Automatically remaps the input to use the full available terrain or mask range.");
	Descriptor.Inputs.Add(AutoLevelAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(AutoLevelAnyPort(TEXT("Out"), TEXT("Out")));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::AutoLevel, EvaluateAutoLevelNode);
}
