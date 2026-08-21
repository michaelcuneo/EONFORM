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

	FGaeaTerrainParameterDescriptor ClampNumberParameter(
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

	FGaeaTerrainParameterDescriptor ClampNameParameter(
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

	FGaeaTerrainParameterDescriptor ClampBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float ClampApply(float NormalizedHeight, float MinHeight, float MaxHeight, FName Operation)
	{
		const float Span = FMath::Max(MaxHeight - MinHeight, UE_SMALL_NUMBER);

		if (Operation == TEXT("Clip"))
		{
			return FMath::Clamp(NormalizedHeight, MinHeight, MaxHeight);
		}

		if (Operation == TEXT("Extend"))
		{
			return FMath::Clamp((NormalizedHeight - MinHeight) / Span, 0.0f, 1.0f);
		}

		// Gaea's Clamp operation proportionally compresses the terrain into the
		// requested altitude range rather than hard-clipping the extremes.
		return MinHeight + FMath::Clamp(NormalizedHeight, 0.0f, 1.0f) * Span;
	}

	bool EvaluateClampNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
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

		const float MinHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Min"), 0.0)), 0.0f, 1.0f);
		const float MaxHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Max"), 1.0)), MinHeight, 1.0f);
		const FName Operation = Node.GetName(TEXT("Operation"), TEXT("Clamp"));
		const bool bDropToFloor = Node.GetBool(TEXT("DropToFloor"), false);

		FGaeaScalarField ResultHeight = *Height;
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float SignedHeight = Height->AtInterior(X, Y);
				const float NormalizedHeight = FMath::Clamp(SignedHeight * 0.5f + 0.5f, 0.0f, 1.0f);
				float Adjusted = ClampApply(NormalizedHeight, MinHeight, MaxHeight, Operation);
				if (bDropToFloor)
				{
					Adjusted = FMath::Clamp((Adjusted - MinHeight) / FMath::Max(1.0f - MinHeight, UE_SMALL_NUMBER), 0.0f, 1.0f);
				}
				ResultHeight.AtInterior(X, Y) = Adjusted * 2.0f - 1.0f;
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
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Controls terrain altitude by clamping, clipping, or extending the selected height range.");
	Descriptor.Inputs.Add(ClampTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ClampTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ClampNumberParameter(TEXT("Min"), TEXT("Min"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ClampNumberParameter(TEXT("Max"), TEXT("Max"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(ClampNameParameter(TEXT("Operation"), TEXT("Operation"), TEXT("Clamp"), { TEXT("Clamp"), TEXT("Clip"), TEXT("Extend") }));
	Descriptor.Parameters.Add(ClampBooleanParameter(TEXT("DropToFloor"), TEXT("Drop to Floor"), false));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Clamp, EvaluateClampNode);
}
