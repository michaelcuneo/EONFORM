#include "GaeaThermalErosionNode.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosion.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor NumberParameter(
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

	FGaeaTerrainParameterDescriptor IntegerParameter(
		FName Name,
		const TCHAR* DisplayName,
		int64 DefaultValue,
		int64 Minimum,
		int64 Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		return Parameter;
	}

	bool EvaluateThermalErosionNode(
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
			Error = TEXT("Thermal Erosion requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset PreparedDataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings DerivedSettings;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(
			PreparedDataset,
			FMath::Max(Input->HeightScale, 1.0f),
			DerivedSettings,
			&Error))
		{
			return false;
		}

		const FGaeaScalarField* Height = PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Thermal Erosion input dataset has no valid Height field.");
			return false;
		}

		const FGaeaScalarField* AreaMask = nullptr;
		if (const FGaeaTerrainValue* const* MaskPtr = Inputs.Find(TEXT("Mask")))
		{
			const FGaeaTerrainValue* MaskValue = *MaskPtr;
			if (!MaskValue || MaskValue->Type != EGaeaTerrainValueType::ScalarField || !MaskValue->ScalarField.IsValid())
			{
				Error = TEXT("Thermal Erosion Area Mask must be a valid scalar field.");
				return false;
			}
			if (MaskValue->ScalarField.Domain != Height->Domain)
			{
				Error = TEXT("Thermal Erosion Area Mask must use the same domain as Height.");
				return false;
			}
			AreaMask = &MaskValue->ScalarField;
		}

		FGaeaThermalErosionSettings Settings;
		Settings.Iterations = FMath::Clamp<int32>(
			static_cast<int32>(Node.GetInteger(TEXT("Iterations"), Settings.Iterations)),
			1,
			4096);
		Settings.TalusAngleDegrees = FMath::Clamp(
			static_cast<float>(Node.GetNumber(TEXT("TalusAngle"), Settings.TalusAngleDegrees)),
			0.0f,
			89.9f);
		Settings.Strength = FMath::Clamp(
			static_cast<float>(Node.GetNumber(TEXT("Strength"), Settings.Strength)),
			0.0f,
			1.0f);

		FGaeaScalarField ErodedHeight = *Height;
		if (!FGaeaThermalErosion::ApplyInPlace(
			ErodedHeight,
			FMath::Max(Input->HeightScale, 1.0f),
			Settings,
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Thermal),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			AreaMask,
			&Error))
		{
			return false;
		}

		if (!PreparedDataset.SetScalarField(MoveTemp(ErodedHeight)))
		{
			Error = TEXT("Thermal Erosion could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Output = FGaeaTerrainValue::MakeTerrain(MoveTemp(PreparedDataset), Input->HeightScale);
		if (!Output.IsValid())
		{
			Error = TEXT("Thermal Erosion produced an invalid terrain output.");
			return false;
		}
		Out.Outputs.Add(TEXT("Terrain"), MoveTemp(Output));
		return true;
	}
}

void RegisterGaeaThermalErosionNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::ThermalErosion;
	Descriptor.DisplayName = TEXT("Thermal Erosion");
	Descriptor.Category = TEXT("Erosion");
	Descriptor.Description = TEXT("Relaxes slopes above the talus angle by redistributing material downslope.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Inputs.Add(ScalarPort(TEXT("Mask"), TEXT("Area Mask")));
	Descriptor.Outputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Out")));
	Descriptor.Parameters.Add(IntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 12, 1, 4096));
	Descriptor.Parameters.Add(NumberParameter(TEXT("TalusAngle"), TEXT("Talus Angle (deg)"), 34.0, 0.0, 89.9));
	Descriptor.Parameters.Add(NumberParameter(TEXT("Strength"), TEXT("Strength"), 0.35, 0.0, 1.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);

	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::ThermalErosion, EvaluateThermalErosionNode);
}
