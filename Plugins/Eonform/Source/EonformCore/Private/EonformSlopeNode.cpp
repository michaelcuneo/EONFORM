#include "EonformSlopeNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor SlopeNodeTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainPortDescriptor SlopeNodeScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor SlopeNodeNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor SlopeNodeRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor SlopeNodeNameParameter()
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = TEXT("Type");
		Parameter.DisplayName = TEXT("Type");
		Parameter.Type = EEonformTerrainParameterType::Name;
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

	bool EvaluateSlopeNodeCurrent(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Slope requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
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

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("SlopeMask");
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Mask;
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
		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterEonformSlopeNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Slope;
	Descriptor.DisplayName = TEXT("Slope");
	Descriptor.Category = TEXT("Derive");
	Descriptor.Description = TEXT("Creates a selection mask for terrain within a slope range, with Accurate or Normalized evaluation and micro-detail accenting.");
	Descriptor.Inputs.Add(SlopeNodeTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(SlopeNodeScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(SlopeNodeRangeParameter(TEXT("Range"), TEXT("Range"), 0.0, 45.0, 0.0, 90.0));
	Descriptor.Parameters.Add(SlopeNodeNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 5.0, 0.0, 45.0));
	Descriptor.Parameters.Add(SlopeNodeNameParameter());
	Descriptor.Parameters.Add(SlopeNodeNumberParameter(TEXT("MicroAccent"), TEXT("Micro Accent"), 0.0, 0.0, 1.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Slope, EvaluateSlopeNodeCurrent);
}
