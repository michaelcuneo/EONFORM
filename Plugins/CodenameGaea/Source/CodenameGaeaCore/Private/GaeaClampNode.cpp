#include "GaeaClampNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor ClampTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ClampRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Range;
		Parameter.DefaultRangeMin = DefaultMin;
		Parameter.DefaultRangeMax = DefaultMax;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor ClampNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Standard"));
		Parameter.NameOptions.Add(TEXT("Normalized"));
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor ClampBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	bool EvaluateClampNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Clamp requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
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

		FGaeaScalarField ResultHeight = *Height;
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

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("Clamp could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Clamp produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value));
		return true;
	}
}

void RegisterGaeaClampNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Clamp;
	Descriptor.DisplayName = TEXT("Clamp");
	Descriptor.Category = TEXT("Modify");
	Descriptor.Description = TEXT("Restricts terrain values to a selected range, optionally normalizing or dropping the result.");
	Descriptor.Inputs.Add(ClampTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ClampTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ClampNameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Standard")));
	Descriptor.Parameters.Add(ClampRangeParameter(TEXT("Value"), TEXT("Value"), 0.0, 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ClampBooleanParameter(TEXT("Drop"), TEXT("Drop"), false));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Clamp, EvaluateClampNode);
}
