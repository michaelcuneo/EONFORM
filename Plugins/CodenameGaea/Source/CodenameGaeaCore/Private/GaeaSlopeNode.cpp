#include "GaeaSlopeNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor SlopeNodeTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor SlopeNodeScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor SlopeNodeNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor SlopeNodeRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor SlopeNodeNameParameter()
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = TEXT("Type");
		Parameter.DisplayName = TEXT("Type");
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = TEXT("Accurate");
		Parameter.NameOptions.Add(TEXT("Accurate"));
		Parameter.NameOptions.Add(TEXT("Normalized"));
		return Parameter;
	}

	float SlopeNodeSmooth01(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float SlopeNodeRangeWeight(float Value, float Minimum, float Maximum, float Falloff)
	{
		if (Value >= Minimum && Value <= Maximum) return 1.0f;
		if (Falloff <= UE_SMALL_NUMBER) return 0.0f;
		if (Value < Minimum && Value > Minimum - Falloff) return SlopeNodeSmooth01((Value - (Minimum - Falloff)) / Falloff);
		if (Value > Maximum && Value < Maximum + Falloff) return 1.0f - SlopeNodeSmooth01((Value - Maximum) / Falloff);
		return 0.0f;
	}

	bool EvaluateSlopeNodeCurrent(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Slope requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Slope input terrain has no valid Height field.");
			return false;
		}

		const FVector2d CellSize = Height->Domain.GetCellSize();
		if (CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
		{
			Error = TEXT("Slope input terrain has invalid grid spacing.");
			return false;
		}

		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMin"), 0.0)), 0.0f, 90.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMax"), 45.0)), Minimum, 90.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 5.0)), 0.0f, 45.0f);
		const FName Type = Node.GetName(TEXT("Type"), TEXT("Accurate"));
		if (Type != TEXT("Accurate") && Type != TEXT("Normalized"))
		{
			Error = TEXT("Slope Type must be Accurate or Normalized.");
			return false;
		}
		const float MicroAccent = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MicroAccent"), 0.0)), 0.0f, 1.0f);
		const float HeightScale = FMath::Max(Input->HeightScale, 1.0f);

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("SlopeMask");
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
				const float DX = (Height->AtInterior(XR, Y) - Height->AtInterior(XL, Y)) * HeightScale
					/ FMath::Max(static_cast<float>(XR - XL) * static_cast<float>(CellSize.X), UE_SMALL_NUMBER);
				const float DY = (Height->AtInterior(X, YU) - Height->AtInterior(X, YD)) * HeightScale
					/ FMath::Max(static_cast<float>(YU - YD) * static_cast<float>(CellSize.Y), UE_SMALL_NUMBER);
				float Degrees = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
				if (Type == TEXT("Normalized")) Degrees = FMath::Clamp(Degrees / 90.0f, 0.0f, 1.0f) * 90.0f;

				const float Center = Height->AtInterior(X, Y);
				const float LocalMean = 0.25f * (Height->AtInterior(XL, Y) + Height->AtInterior(XR, Y) + Height->AtInterior(X, YD) + Height->AtInterior(X, YU));
				Degrees = FMath::Clamp(Degrees + FMath::Abs(Center - LocalMean) * 90.0f * MicroAccent, 0.0f, 90.0f);
				Mask.AtInterior(X, Y) = SlopeNodeRangeWeight(Degrees, Minimum, Maximum, Falloff);
			}
		}

		if (!Mask.IsValid())
		{
			Error = TEXT("Slope produced an invalid mask.");
			return false;
		}
		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterGaeaSlopeNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Slope;
	Descriptor.DisplayName = TEXT("Slope");
	Descriptor.Category = TEXT("Derive");
	Descriptor.Description = TEXT("Creates a selection mask for terrain within a slope range, with Accurate or Normalized evaluation and micro-detail accenting.");
	Descriptor.Inputs.Add(SlopeNodeTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SlopeNodeScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(SlopeNodeRangeParameter(TEXT("Range"), TEXT("Range"), 0.0, 45.0, 0.0, 90.0));
	Descriptor.Parameters.Add(SlopeNodeNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 5.0, 0.0, 45.0));
	Descriptor.Parameters.Add(SlopeNodeNameParameter());
	Descriptor.Parameters.Add(SlopeNodeNumberParameter(TEXT("MicroAccent"), TEXT("Micro Accent"), 0.0, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Slope, EvaluateSlopeNodeCurrent);
}
