#include "EonformFlipNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor FlipTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor FlipNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("X"));
		Parameter.NameOptions.Add(TEXT("Y"));
		Parameter.NameOptions.Add(TEXT("XY"));
		return Parameter;
	}

	bool FlipField(const FEonformScalarField& Source, FName Type, FEonformScalarField& OutField)
	{
		if (!Source.IsValid()) return false;
		OutField = Source;
		const bool bX = Type == TEXT("X") || Type == TEXT("XY");
		const bool bY = Type == TEXT("Y") || Type == TEXT("XY");
		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 SourceX = bX ? Width - 1 - X : X;
				const int32 SourceY = bY ? Height - 1 - Y : Y;
				OutField.AtInterior(X, Y) = Source.AtInterior(SourceX, SourceY);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateFlipNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Flip requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FName Type = Node.GetName(TEXT("Type"), TEXT("X"));
		if (Type != TEXT("X") && Type != TEXT("Y") && Type != TEXT("XY"))
		{
			Error = TEXT("Flip Type must be X, Y, or XY.");
			return false;
		}

		FEonformTerrainDataset Dataset;
		TArray<FName> FieldNames;
		Input->TerrainDataset.GetScalarFieldNames(FieldNames);
		for (const FName FieldName : FieldNames)
		{
			const FEonformScalarField* Field = Input->TerrainDataset.FindScalarField(FieldName);
			if (!Field || !Field->IsValid())
			{
				Error = FString::Printf(TEXT("Flip input field '%s' is invalid."), *FieldName.ToString());
				return false;
			}
			FEonformScalarField Flipped;
			if (!FlipField(*Field, Type, Flipped) || !Dataset.SetScalarField(MoveTemp(Flipped)))
			{
				Error = FString::Printf(TEXT("Flip could not publish field '%s'."), *FieldName.ToString());
				return false;
			}
		}

		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Flip produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformFlipNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Flip;
	Descriptor.DisplayName = TEXT("Flip");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Flips terrain on the X axis, Y axis, or both axes.");
	Descriptor.Inputs.Add(FlipTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(FlipTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(FlipNameParameter(TEXT("Type"), TEXT("Type"), TEXT("X")));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Flip, EvaluateFlipNode);
}
