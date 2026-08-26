#include "EonformClampNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor ClampTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor ClampRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Range;
		Parameter.DefaultRangeMin = DefaultMin;
		Parameter.DefaultRangeMax = DefaultMax;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor ClampNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Standard"));
		Parameter.NameOptions.Add(TEXT("Normalized"));
		return Parameter;
	}

	FEonformTerrainParameterDescriptor ClampBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	bool EvaluateClampNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Clamp requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Clamp input terrain has no valid Height field.");
			return false;
		}

		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Standard"));
		if (Mode != TEXT("Standard") && Mode != TEXT("Normalized"))
		{
			Error = TEXT("Clamp Mode must be Standard or Normalized.");
			return false;
		}
		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ValueMin"), 0.0)), 0.0f, 1.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ValueMax"), 1.0)), Minimum, 1.0f);
		const bool bDrop = Node.GetBool(TEXT("Drop"), false);
		const float Span = FMath::Max(Maximum - Minimum, UE_SMALL_NUMBER);

		FEonformScalarField ResultHeight = *Height;
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float Original = FMath::Clamp(Height->AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				const float Clamped = FMath::Clamp(Original, Minimum, Maximum);
				float Adjusted = Mode == TEXT("Normalized") ? (Clamped - Minimum) / Span : Clamped;
				if (bDrop && Mode == TEXT("Standard"))
				{
					Adjusted = FMath::Clamp(Adjusted - Minimum, 0.0f, 1.0f);
				}
				ResultHeight.AtInterior(X, Y) = FMath::Clamp(Adjusted * 2.0f - 1.0f, -1.0f, 1.0f);
			}
		}

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Clamp could not publish its Height field.");
			return false;
		}

		FEonformTerrainValue Value = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Clamp produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}
}

void RegisterEonformClampNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Clamp;
	Descriptor.DisplayName = TEXT("Clamp");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Restricts terrain values to a selected range, optionally normalizing or dropping the result.");
	Descriptor.Inputs.Add(ClampTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ClampTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ClampNameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Standard")));
	Descriptor.Parameters.Add(ClampRangeParameter(TEXT("Value"), TEXT("Value"), 0.0, 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ClampBooleanParameter(TEXT("Drop"), TEXT("Drop"), false));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);

	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Clamp, EvaluateClampNode);
}
