#include "GaeaAngleNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor AngleNodeTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor AngleNodeScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor AngleNodeNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor AngleNodeRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
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

	float AngleNodeSmooth01(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float AngleNodeRangeWeight(float Value, float Minimum, float Maximum, float Falloff)
	{
		if (Value >= Minimum && Value <= Maximum) return 1.0f;
		if (Falloff <= UE_SMALL_NUMBER) return 0.0f;
		if (Value < Minimum && Value > Minimum - Falloff)
		{
			return AngleNodeSmooth01((Value - (Minimum - Falloff)) / Falloff);
		}
		if (Value > Maximum && Value < Maximum + Falloff)
		{
			return 1.0f - AngleNodeSmooth01((Value - Maximum) / Falloff);
		}
		return 0.0f;
	}

	float AngleNodeAngularDistance(float A, float B)
	{
		const float Delta = FMath::Fmod(A - B + 540.0f, 360.0f) - 180.0f;
		return FMath::Abs(Delta);
	}

	bool EvaluateAngleNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Angle requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Angle input terrain has no valid Height field.");
			return false;
		}

		const FVector2d CellSize = Height->Domain.GetCellSize();
		if (CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
		{
			Error = TEXT("Angle input terrain has invalid grid spacing.");
			return false;
		}

		const float Azimuth = FMath::Fmod(FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Azimuth"), 0.0)), 0.0f), 360.0f);
		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMin"), 0.0)), 0.0f, 180.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMax"), 45.0)), Minimum, 180.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 5.0)), 0.0f, 180.0f);
		const float HeightScale = FMath::Max(Input->HeightScale, 1.0f);

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("AngleMask");
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

				if (FMath::Abs(DX) <= UE_SMALL_NUMBER && FMath::Abs(DY) <= UE_SMALL_NUMBER)
				{
					Mask.AtInterior(X, Y) = 0.0f;
					continue;
				}

				float Aspect = FMath::RadiansToDegrees(FMath::Atan2(-DX, -DY));
				if (Aspect < 0.0f) Aspect += 360.0f;
				const float Difference = AngleNodeAngularDistance(Aspect, Azimuth);
				Mask.AtInterior(X, Y) = AngleNodeRangeWeight(Difference, Minimum, Maximum, Falloff);
			}
		}

		if (!Mask.IsValid())
		{
			Error = TEXT("Angle produced an invalid mask.");
			return false;
		}

		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterGaeaAngleNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Angle;
	Descriptor.DisplayName = TEXT("Angle");
	Descriptor.Category = TEXT("Derive");
	Descriptor.Description = TEXT("Creates a selection mask covering terrain areas facing the specified angular range.");
	Descriptor.Inputs.Add(AngleNodeTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(AngleNodeScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(AngleNodeNumberParameter(TEXT("Azimuth"), TEXT("Azimuth"), 0.0, 0.0, 360.0));
	Descriptor.Parameters.Add(AngleNodeRangeParameter(TEXT("Range"), TEXT("Range"), 0.0, 45.0, 0.0, 180.0));
	Descriptor.Parameters.Add(AngleNodeNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 5.0, 0.0, 180.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Angle, EvaluateAngleNode);
}
