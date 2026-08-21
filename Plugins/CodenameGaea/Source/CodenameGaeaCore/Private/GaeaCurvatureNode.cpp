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

	FGaeaTerrainParameterDescriptor CurvatureNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor CurvatureRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor CurvatureNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Horizontal"));
		Parameter.NameOptions.Add(TEXT("Vertical"));
		Parameter.NameOptions.Add(TEXT("Average"));
		return Parameter;
	}

	float CurvatureSmooth01(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float CurvatureRangeWeight(float Value, float Minimum, float Maximum, float Falloff)
	{
		if (Value >= Minimum && Value <= Maximum) return 1.0f;
		if (Falloff <= UE_SMALL_NUMBER) return 0.0f;
		if (Value < Minimum && Value > Minimum - Falloff)
		{
			return CurvatureSmooth01((Value - (Minimum - Falloff)) / Falloff);
		}
		if (Value > Maximum && Value < Maximum + Falloff)
		{
			return 1.0f - CurvatureSmooth01((Value - Maximum) / Falloff);
		}
		return 0.0f;
	}

	bool EvaluateCurvatureNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
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

		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMin"), 0.0)), 0.0f, 1.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMax"), 1.0)), Minimum, 1.0f);
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Average"));
		if (Type != TEXT("Horizontal") && Type != TEXT("Vertical") && Type != TEXT("Average"))
		{
			Error = TEXT("Curvature Type must be Horizontal, Vertical, or Average.");
			return false;
		}
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.1)), 0.0f, 1.0f);
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
					(Center - 0.5f * (Height->AtInterior(XL, Y) + Height->AtInterior(XR, Y))) * HeightScale
					/ FMath::Max(static_cast<float>(CellSize.X), UE_SMALL_NUMBER), 0.0f, 1.0f);
				const float Vertical = FMath::Clamp(
					(Center - 0.5f * (Height->AtInterior(X, YD) + Height->AtInterior(X, YU))) * HeightScale
					/ FMath::Max(static_cast<float>(CellSize.Y), UE_SMALL_NUMBER), 0.0f, 1.0f);

				float Curvature = 0.5f * (Horizontal + Vertical);
				if (Type == TEXT("Horizontal")) Curvature = Horizontal;
				else if (Type == TEXT("Vertical")) Curvature = Vertical;
				Mask.AtInterior(X, Y) = FMath::Clamp(CurvatureRangeWeight(Curvature, Minimum, Maximum, Falloff), 0.0f, 1.0f);
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
	Descriptor.Category = TEXT("Derive");
	Descriptor.Description = TEXT("Creates a mask selecting convex terrain curvature within the specified range.");
	Descriptor.Inputs.Add(CurvatureTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(CurvatureScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(CurvatureRangeParameter(TEXT("Range"), TEXT("Range"), 0.0, 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CurvatureNameParameter(TEXT("Type"), TEXT("Type"), TEXT("Average")));
	Descriptor.Parameters.Add(CurvatureNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.1, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Curvature, EvaluateCurvatureNode);
}
