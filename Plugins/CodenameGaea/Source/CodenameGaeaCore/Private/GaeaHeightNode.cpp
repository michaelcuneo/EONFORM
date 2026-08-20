#include "GaeaHeightNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor HeightNodeTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor HeightNodeScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor HeightNodeNumberParameter(
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

	FGaeaTerrainParameterDescriptor HeightNodeBooleanParameter(
		FName Name,
		const TCHAR* DisplayName,
		bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	float HeightNodeRangeWeight(float Value, float Minimum, float Maximum, float Falloff)
	{
		if (Value >= Minimum && Value <= Maximum)
		{
			return 1.0f;
		}
		if (Value > Maximum && Falloff > UE_SMALL_NUMBER && Value < Maximum + Falloff)
		{
			const float T = FMath::Clamp((Value - Maximum) / Falloff, 0.0f, 1.0f);
			const float Smooth = T * T * (3.0f - 2.0f * T);
			return 1.0f - Smooth;
		}
		return 0.0f;
	}

	bool EvaluateHeightNode(
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
			Error = TEXT("Height requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Height input terrain has no valid Height field.");
			return false;
		}

		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Min"), 0.0)), 0.0f, 1.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Max"), 1.0)), Minimum, 1.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.05)), 0.0f, 1.0f);
		const bool bNormalized = Node.GetBool(TEXT("Normalized"), false);

		float CurrentMin = TNumericLimits<float>::Max();
		float CurrentMax = TNumericLimits<float>::Lowest();
		if (bNormalized)
		{
			for (const float Value : Height->Values)
			{
				CurrentMin = FMath::Min(CurrentMin, Value);
				CurrentMax = FMath::Max(CurrentMax, Value);
			}
		}
		const float CurrentRange = bNormalized
			? FMath::Max(CurrentMax - CurrentMin, UE_SMALL_NUMBER)
			: 1.0f;

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("HeightMask");
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Mask;
		Mask.Initialize(Height->Domain, Descriptor);

		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float RawHeight = Height->AtInterior(X, Y);
				const float SelectionHeight = bNormalized
					? FMath::Clamp((RawHeight - CurrentMin) / CurrentRange, 0.0f, 1.0f)
					: FMath::Clamp(RawHeight * 0.5f + 0.5f, 0.0f, 1.0f);
				Mask.AtInterior(X, Y) = HeightNodeRangeWeight(SelectionHeight, Minimum, Maximum, Falloff);
			}
		}

		if (!Mask.IsValid())
		{
			Error = TEXT("Height produced an invalid mask.");
			return false;
		}

		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterGaeaHeightNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Height;
	Descriptor.DisplayName = TEXT("Height");
	Descriptor.Category = TEXT("Data");
	Descriptor.Description = TEXT("Creates a selection mask within the specified terrain height range.");
	Descriptor.Inputs.Add(HeightNodeTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(HeightNodeScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(HeightNodeNumberParameter(TEXT("Min"), TEXT("Min"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(HeightNodeNumberParameter(TEXT("Max"), TEXT("Max"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(HeightNodeNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.05, 0.0, 1.0));
	Descriptor.Parameters.Add(HeightNodeBooleanParameter(TEXT("Normalized"), TEXT("Normalized"), false));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Height, EvaluateHeightNode);
}
