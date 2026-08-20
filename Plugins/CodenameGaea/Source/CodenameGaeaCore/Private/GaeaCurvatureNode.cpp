#include "GaeaCurvatureNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor CurvatureTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor CurvatureScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor CurvatureNumberParameter(
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

	FGaeaTerrainParameterDescriptor CurvatureNameParameter(
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

	FGaeaTerrainParameterDescriptor CurvatureBooleanParameter(
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

	float CurvatureRangeWeight(float Value, float Minimum, float Maximum, float Falloff)
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

	bool EvaluateCurvatureNode(
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
			Error = TEXT("Curvature requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Curvature input terrain has no valid Height field.");
			return false;
		}

		const FVector2d CellSize = Height->Domain.GetCellSize();
		if (CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
		{
			Error = TEXT("Curvature input terrain has invalid grid spacing.");
			return false;
		}

		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Min"), 0.0)), 0.0f, 1.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Max"), 1.0)), Minimum, 1.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.1)), 0.0f, 1.0f);
		const FName CurvatureType = Node.GetName(TEXT("CurvatureType"), TEXT("Average"));
		const bool bInvert = Node.GetBool(TEXT("Invert"), false);
		const float HeightScale = FMath::Max(Input->HeightScale, 1.0f);

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("CurvatureMask");
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Mask;
		Mask.Initialize(Height->Domain, Descriptor);

		const FIntPoint Dimensions = Height->Domain.Dimensions;
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const int32 XL = FMath::Max(0, X - 1);
				const int32 XR = FMath::Min(Dimensions.X - 1, X + 1);
				const int32 YD = FMath::Max(0, Y - 1);
				const int32 YU = FMath::Min(Dimensions.Y - 1, Y + 1);
				const float Center = Height->AtInterior(X, Y);
				const float Horizontal = FMath::Clamp(
					(Center - 0.5f * (Height->AtInterior(XL, Y) + Height->AtInterior(XR, Y)))
					* HeightScale / FMath::Max(static_cast<float>(CellSize.X), UE_SMALL_NUMBER),
					0.0f,
					1.0f);
				const float Vertical = FMath::Clamp(
					(Center - 0.5f * (Height->AtInterior(X, YD) + Height->AtInterior(X, YU)))
					* HeightScale / FMath::Max(static_cast<float>(CellSize.Y), UE_SMALL_NUMBER),
					0.0f,
					1.0f);

				float Curvature = 0.5f * (Horizontal + Vertical);
				if (CurvatureType == TEXT("Horizontal")) Curvature = Horizontal;
				else if (CurvatureType == TEXT("Vertical")) Curvature = Vertical;

				float Weight = CurvatureRangeWeight(Curvature, Minimum, Maximum, Falloff);
				if (bInvert) Weight = 1.0f - Weight;
				Mask.AtInterior(X, Y) = FMath::Clamp(Weight, 0.0f, 1.0f);
			}
		}

		if (!Mask.IsValid())
		{
			Error = TEXT("Curvature produced an invalid mask.");
			return false;
		}

		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterGaeaCurvatureNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Curvature;
	Descriptor.DisplayName = TEXT("Curvature");
	Descriptor.Category = TEXT("Data");
	Descriptor.Description = TEXT("Creates a mask selecting convex terrain curvature within the specified range.");
	Descriptor.Inputs.Add(CurvatureTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(CurvatureScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(CurvatureNumberParameter(TEXT("Min"), TEXT("Min"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CurvatureNumberParameter(TEXT("Max"), TEXT("Max"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CurvatureNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.1, 0.0, 1.0));
	Descriptor.Parameters.Add(CurvatureNameParameter(
		TEXT("CurvatureType"),
		TEXT("Curvature Type"),
		TEXT("Average"),
		{ TEXT("Horizontal"), TEXT("Vertical"), TEXT("Average") }));
	Descriptor.Parameters.Add(CurvatureBooleanParameter(TEXT("Invert"), TEXT("Invert"), false));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Curvature, EvaluateCurvatureNode);
}
