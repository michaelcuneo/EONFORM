#include "EonformAngleNode.h"

#include "EonformRegionalFieldSampling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor AngleNodeTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainPortDescriptor AngleNodeScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor AngleNodeNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FEonformTerrainParameterDescriptor AngleNodeRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum)
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

	bool EvaluateAngleNode(
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
			Error = TEXT("Angle requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Angle input terrain has no valid Height field.");
			return false;
		}
		if (Context.HasRegion() && Height->Domain.BorderSamples < EonformAngleNode::RequiredBorderSamples())
		{
			Error = TEXT("Regional Angle requires one dependency-border sample.");
			return false;
		}

		const FVector2d CellSize = Height->Domain.GetCellSize();
		if (CellSize.X <= UE_SMALL_NUMBER || CellSize.Y <= UE_SMALL_NUMBER)
		{
			Error = TEXT("Angle input terrain has invalid grid spacing.");
			return false;
		}

		FEonformGridDomain WorldDomain;
		if (!EonformRegionalFieldSampling::ResolveWorldDomain(*Height, Context, WorldDomain, Error)) return false;

		const float Azimuth = FMath::Fmod(FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Azimuth"), 0.0)), 0.0f), 360.0f);
		const float Minimum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMin"), 0.0)), 0.0f, 180.0f);
		const float Maximum = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RangeMax"), 45.0)), Minimum, 180.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 5.0)), 0.0f, 180.0f);
		const float HeightScale = FMath::Max(Input->HeightScale, 1.0f);

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("AngleMask");
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
				const FIntPoint LeftCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(*Height, CenterCoord.X - 1, CenterCoord.Y, WorldDomain);
				const FIntPoint RightCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(*Height, CenterCoord.X + 1, CenterCoord.Y, WorldDomain);
				const FIntPoint DownCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(*Height, CenterCoord.X, CenterCoord.Y - 1, WorldDomain);
				const FIntPoint UpCoord = EonformRegionalFieldSampling::ResolveStorageCoordinate(*Height, CenterCoord.X, CenterCoord.Y + 1, WorldDomain);

				const float DX = (Height->AtStorage(RightCoord.X, RightCoord.Y) - Height->AtStorage(LeftCoord.X, LeftCoord.Y)) * HeightScale
					/ FMath::Max(static_cast<float>(RightCoord.X - LeftCoord.X) * static_cast<float>(CellSize.X), UE_SMALL_NUMBER);
				const float DY = (Height->AtStorage(UpCoord.X, UpCoord.Y) - Height->AtStorage(DownCoord.X, DownCoord.Y)) * HeightScale
					/ FMath::Max(static_cast<float>(UpCoord.Y - DownCoord.Y) * static_cast<float>(CellSize.Y), UE_SMALL_NUMBER);

				if (FMath::Abs(DX) <= UE_SMALL_NUMBER && FMath::Abs(DY) <= UE_SMALL_NUMBER)
				{
					Mask.AtStorage(X, Y) = 0.0f;
					continue;
				}

				float Aspect = FMath::RadiansToDegrees(FMath::Atan2(-DX, -DY));
				if (Aspect < 0.0f) Aspect += 360.0f;
				const float Difference = AngleNodeAngularDistance(Aspect, Azimuth);
				Mask.AtStorage(X, Y) = AngleNodeRangeWeight(Difference, Minimum, Maximum, Falloff);
			}
		}

		if (!Mask.IsValid())
		{
			Error = TEXT("Angle produced an invalid mask.");
			return false;
		}

		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}
}

void RegisterEonformAngleNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Angle;
	Descriptor.DisplayName = TEXT("Angle");
	Descriptor.Category = TEXT("Derive");
	Descriptor.Description = TEXT("Creates a selection mask covering terrain areas facing the specified angular range.");
	Descriptor.Inputs.Add(AngleNodeTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(AngleNodeScalarPort(TEXT("Mask"), TEXT("Mask")));
	Descriptor.Parameters.Add(AngleNodeNumberParameter(TEXT("Azimuth"), TEXT("Azimuth"), 0.0, 0.0, 360.0));
	Descriptor.Parameters.Add(AngleNodeRangeParameter(TEXT("Range"), TEXT("Range"), 0.0, 45.0, 0.0, 180.0));
	Descriptor.Parameters.Add(AngleNodeNumberParameter(TEXT("Falloff"), TEXT("Falloff"), 5.0, 0.0, 180.0));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);

	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Angle, EvaluateAngleNode);
}
