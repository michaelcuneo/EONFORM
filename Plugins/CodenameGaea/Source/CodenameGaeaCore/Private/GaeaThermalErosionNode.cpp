#include "GaeaThermalErosionNode.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosion.h"

namespace
{
	FGaeaTerrainPortDescriptor ThermalTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor ThermalNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor ThermalIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor ThermalBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue, const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	bool EvaluateThermalErosionNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Thermal requires a valid terrain input 'Terrain'.");
			return false;
		}

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), 12)), 1, 4096);
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), 0.35)), 0.0f, 1.0f);
		const float Anisotropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f);
		const float Angle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Angle"), 34.0)), 0.0f, 89.9f);
		const float Settling = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Settling"), 0.5)), 0.0f, 1.0f);
		const float SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), 0.0)), 0.0f, 1.0f);
		const float FeatureScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FeatureScale"), 1.0)), 0.25f, 8.0f);
		const bool bRealScale = Node.GetBool(TEXT("RealScale"), true);
		const float TerrainScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TerrainScale"), 1.0)), 0.01f, 100.0f);
		const float Verticality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Verticality"), 1.0)), 0.01f, 10.0f);

		FGaeaThermalErosionSettings Settings;
		Settings.Iterations = Duration;
		Settings.TalusAngleDegrees = FMath::Clamp(Angle / FMath::Max(Verticality, 0.01f), 0.0f, 89.9f);
		const float ScaleResponse = bRealScale ? FMath::Clamp(FeatureScale / FMath::Max(TerrainScale, 0.01f), 0.1f, 4.0f) : FeatureScale;
		Settings.Strength = FMath::Clamp(Strength * FMath::Lerp(1.0f, 0.65f, Anisotropy) * FMath::Lerp(0.75f, 1.25f, Settling) * ScaleResponse * (1.0f - 0.5f * SedimentRemoval), 0.0f, 1.0f);

		FGaeaTerrainDataset PreparedDataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings DerivedSettings;
		DerivedSettings.ProcessMasks.ThermalTalusAngleDegrees = Settings.TalusAngleDegrees;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(PreparedDataset, FMath::Max(Input->HeightScale, 1.0f), DerivedSettings, &Error)) return false;

		const FGaeaScalarField* Height = PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Thermal input dataset has no valid Height field.");
			return false;
		}

		FGaeaScalarField ErodedHeight = *Height;
		if (!FGaeaThermalErosion::ApplyInPlace(
			ErodedHeight,
			FMath::Max(Input->HeightScale, 1.0f),
			Settings,
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Thermal),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			nullptr,
			&Error))
		{
			return false;
		}

		if (!PreparedDataset.SetScalarField(MoveTemp(ErodedHeight)))
		{
			Error = TEXT("Thermal could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Output = FGaeaTerrainValue::MakeTerrain(MoveTemp(PreparedDataset), Input->HeightScale);
		if (!Output.IsValid())
		{
			Error = TEXT("Thermal produced an invalid terrain output.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Output));
		return true;
	}
}

void RegisterGaeaThermalErosionNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::ThermalErosion;
	Descriptor.DisplayName = TEXT("Thermal");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Simulates thermal weathering and material settling on terrain slopes.");
	Descriptor.Inputs.Add(ThermalTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(ThermalTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(ThermalIntegerParameter(TEXT("Duration"), TEXT("Duration"), 12, 1, 4096, TEXT("Erosion")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("Strength"), TEXT("Strength"), 0.35, 0.0, 1.0, TEXT("Erosion")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0, TEXT("Erosion")));
	Descriptor.Parameters.Add(ThermalIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Erosion")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("Angle"), TEXT("Angle"), 34.0, 0.0, 89.9, TEXT("Talus")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("Settling"), TEXT("Settling"), 0.5, 0.0, 1.0, TEXT("Talus")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Talus")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("FeatureScale"), TEXT("Feature Scale"), 1.0, 0.25, 8.0, TEXT("Scale")));
	Descriptor.Parameters.Add(ThermalBooleanParameter(TEXT("RealScale"), TEXT("Real Scale"), true, TEXT("Scale")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("TerrainScale"), TEXT("Terrain Scale"), 1.0, 0.01, 100.0, TEXT("Scale")));
	Descriptor.Parameters.Add(ThermalNumberParameter(TEXT("Verticality"), TEXT("Verticality"), 1.0, 0.01, 10.0, TEXT("Scale")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::ThermalErosion, EvaluateThermalErosionNode);
}
