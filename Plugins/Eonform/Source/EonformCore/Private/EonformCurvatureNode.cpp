#include "EonformCurvatureNode.h"

#include "EonformRegionalFieldSampling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor CurvatureTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainPortDescriptor CurvatureScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor CurvatureNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor CurvatureRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor CurvatureNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
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

	bool EvaluateCurvatureNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Curvature requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Curvature input terrain has no valid Height field.");
			return false;
		}
		if (Context.HasRegion() && Height->Domain.BorderSamples < EonformCurvatureNode::RequiredBorderSamples())
		{
			Error = TEXT("Regional Curvature requires one dependency-border sample.");
			return false;
		}

		const FVector2d CellSize = Height->Domain.GetCellSize();
		if (CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
		{
			Error = TEXT("Curvature input terrain has invalid grid spacing.");
			return false;
		}

		FEonformGridDomain WorldDomain;
		if (!EonformRegionalFieldSampling::ResolveWorldDomain(*Height, Context, WorldDomain, Error)) return false;

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

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("CurvatureMask");
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Mask;
		Mask.Initialize(Height->Domain, Descriptor);

		const FIntPoint Storage = Height->Domain.GetStorageDimensions();
		for (int32 Y = 0; Y < Storage.Y; ++Y)
		{
			for (int32 X = 0; X < Storage.X; ++X)
			{
				const FIntPoint CenterCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(*Height, X, Y, WorldDomain);
				const float Center = Height->AtStorage(CenterCoord.X, CenterCoord.Y);
				const float Left = EonformRegionalFieldSampling::SampleOffset(*Height, CenterCoord, -1, 0, WorldDomain);
				const float Right = EonformRegionalFieldSampling::SampleOffset(*Height, CenterCoord, 1, 0, WorldDomain);
				const float Down = EonformRegionalFieldSampling::SampleOffset(*Height, CenterCoord, 0, -1, WorldDomain);
				const float Up = EonformRegionalFieldSampling::SampleOffset(*Height, CenterCoord, 0, 1, WorldDomain);
				const float Horizontal = FMath::Clamp(
					(Center - 0.5f * (Left + Right)) * HeightScale
					/ FMath::Max(static_cast<float>(CellSize.X), UE_SMALL_NUMBER), 0.0f, 1.0f);
				const float Vertical = FMath::Clamp(
					(Center - 0.5f * (Down + Up)) * HeightScale
					/ FMath::Max(static_cast<float>(CellSize.Y), UE_SMALL_NUMBER), 0.0f, 1.0f);

				float Curvature = 0.5f * (Horizontal + Vertical);
				if (Type == TEXT("Horizontal")) Curvature = Horizontal;
				else if (Type == TEXT("Vertical")) Curvature = Vertical;
				Mask.AtStorage(X, Y) = FMath::Clamp(CurvatureRangeWeight(Curvature, Minimum, Maximum, Falloff), 0.0f, 1.0f);
			}
		}

		if (!Mask.IsValid())
		{
			Error = TEXT("Curvature produced an invalid mask.");
			return false;
		}

		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterEonformCurvatureNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Curvature;
	Descriptor.DisplayName = TEXT("Curvature");
	Descriptor.Category = TEXT("Derive");
	Descriptor.Description = TEXT("Creates a mask selecting convex terrain curvature within the specified range.");
	Descriptor.Inputs.Add(CurvatureTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(CurvatureScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(CurvatureRangeParameter(TEXT("Range"), TEXT("Range"), 0.0, 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CurvatureNameParameter(TEXT("Type"), TEXT("Type"), TEXT("Average")));
	Descriptor.Parameters.Add(CurvatureNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 0.1, 0.0, 1.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);

	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Curvature, EvaluateCurvatureNode);
}
