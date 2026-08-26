#include "EonformErosionNode.h"

#include "EonformHydraulicErosion.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor ErosionTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainPortDescriptor ErosionScalarPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("ScalarField");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor ErosionNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor ErosionIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor ErosionBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor ErosionNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	const FEonformScalarField* ErosionResolveHeightfield(const FEonformTerrainValue& Value)
	{
		if (Value.Type == EEonformTerrainValueType::ScalarField && Value.ScalarField.IsValid())
		{
			return &Value.ScalarField;
		}
		if (Value.Type == EEonformTerrainValueType::Terrain && Value.IsValid())
		{
			return Value.TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		}
		return nullptr;
	}

	bool ErosionBuildBiasMask(const FEonformTerrainNode& Node, const FEonformScalarField& Height, float HeightScale, const FEonformScalarField* AreaInput, FEonformScalarField& OutMask)
	{
		const FName BiasType = Node.GetName(TEXT("BiasType"), TEXT("Altitude"));
		const float Bias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Bias"), 0.5)), 0.0f, 1.0f);
		const bool bReverse = Node.GetBool(TEXT("Reverse"), false);
		const FVector2d CellSize = Height.Domain.GetCellSize();
		OutMask = Height;
		OutMask.Descriptor.Name = TEXT("ErosionAreaBias");
		OutMask.Descriptor.Unit = EEonformFieldUnit::Normalized;

		for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
			{
				float Value = 0.0f;
				if (BiasType == TEXT("Slope"))
				{
					const int32 XL = FMath::Max(0, X - 1);
					const int32 XR = FMath::Min(Height.Domain.Dimensions.X - 1, X + 1);
					const int32 YD = FMath::Max(0, Y - 1);
					const int32 YU = FMath::Min(Height.Domain.Dimensions.Y - 1, Y + 1);
					const float DX = (Height.AtInterior(XR, Y) - Height.AtInterior(XL, Y)) * HeightScale / FMath::Max(static_cast<float>(XR - XL) * static_cast<float>(CellSize.X), UE_SMALL_NUMBER);
					const float DY = (Height.AtInterior(X, YU) - Height.AtInterior(X, YD)) * HeightScale / FMath::Max(static_cast<float>(YU - YD) * static_cast<float>(CellSize.Y), UE_SMALL_NUMBER);
					Value = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY))) / 90.0f, 0.0f, 1.0f);
				}
				else
				{
					Value = FMath::Clamp(Height.AtInterior(X, Y) * 0.5f + 0.5f, 0.0f, 1.0f);
				}

				Value = FMath::Clamp((Value - Bias) / FMath::Max(1.0f - Bias, UE_SMALL_NUMBER), 0.0f, 1.0f);
				if (bReverse) Value = 1.0f - Value;
				if (AreaInput) Value *= FMath::Clamp(AreaInput->AtInterior(X, Y), 0.0f, 1.0f);
				OutMask.AtInterior(X, Y) = Value;
			}
		}
		return OutMask.IsValid();
	}

	bool EvaluateErosionNodeCurrent(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Erosion requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset PreparedDataset = Input->TerrainDataset;
		FEonformTerrainDerivedDataSettings DerivedSettings;
		if (!FEonformTerrainDerivedData::EnsureHydraulicInputs(
			PreparedDataset,
			FMath::Max(Input->HeightScale, 1.0f),
			Context.PhysicalMetrics,
			DerivedSettings,
			&Error)) return false;
		const FEonformScalarField* Height = PreparedDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Erosion input dataset has no valid Height field.");
			return false;
		}

		const FEonformTerrainValue* const* AreaPtr = Inputs.Find(TEXT("Area"));
		const FEonformTerrainValue* AreaValue = AreaPtr ? *AreaPtr : nullptr;
		const FEonformScalarField* AreaInput = nullptr;
		if (AreaValue)
		{
			AreaInput = ErosionResolveHeightfield(*AreaValue);
			if (!AreaInput || AreaInput->Domain != Height->Domain)
			{
				Error = TEXT("Erosion Area must be a valid heightfield using the terrain domain.");
				return false;
			}
		}

		FEonformHydraulicErosionSettings Settings;
		Settings.Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), Settings.Iterations)), 1, 4096);
		Settings.RockSoftness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RockSoftness"), Settings.RockSoftness)), 0.0f, 1.0f);
		Settings.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), Settings.Strength)), 0.0f, 4.0f);
		Settings.Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), Settings.Downcutting)), 0.0f, 2.0f);
		Settings.Inhibition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Inhibition"), Settings.Inhibition)), 0.0f, 1.0f);
		Settings.BaseLevel = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BaseLevel"), Settings.BaseLevel)), -1.0f, 1.0f);
		const float FeatureScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FeatureScale"), 1.0)), 0.25f, 8.0f);
		const bool bRealScale = Node.GetBool(TEXT("RealScale"), true);
		const float TerrainScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TerrainScale"), 1.0)), 0.01f, 100.0f);
		const float Verticality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Verticality"), 1.0)), 0.01f, 10.0f);
		Settings.FeatureScale = bRealScale ? FMath::Clamp(FeatureScale / TerrainScale, 0.25f, 8.0f) : FeatureScale;
		Settings.Strength = FMath::Clamp(Settings.Strength * Verticality, 0.0f, 4.0f);
		Settings.Debris = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Debris"), Settings.Debris)), 0.0f, 1.0f);
		Settings.Volume = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Volume"), Settings.Volume)), 0.0f, 4.0f);
		Settings.SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), Settings.SedimentRemoval)), 0.0f, 1.0f);
		const FName AreaEffect = Node.GetName(TEXT("AreaEffect"), TEXT("None"));
		if (AreaEffect == TEXT("Erosion Strength")) Settings.SelectiveProcessing = TEXT("ErosionStrength");
		else if (AreaEffect == TEXT("Rock Softness")) Settings.SelectiveProcessing = TEXT("RockSoftness");
		else if (AreaEffect == TEXT("Precipitation Amount")) Settings.SelectiveProcessing = TEXT("Precipitation");
		else Settings.SelectiveProcessing = TEXT("None");
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), Settings.Seed));
		Settings.bAggressiveMode = Node.GetBool(TEXT("AggressiveMode"), Settings.bAggressiveMode);
		Settings.bDeterministic = Node.GetBool(TEXT("Deterministic"), Settings.bDeterministic);

		const FVector2d DomainCellSize = Height->Domain.GetCellSize();
		const double DomainRepresentativeCentimeters = FMath::Max(
			FMath::Min(FMath::Abs(DomainCellSize.X), FMath::Abs(DomainCellSize.Y)),
			UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSampleSpacingMeters = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(
			Height->Domain.Dimensions,
			DomainCellSize);
		const double PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		Settings.PhysicalSampleSpacingMeters = PhysicalSampleSpacingMeters;
		Settings.PhysicalElevationScaleMeters = PhysicalElevationScaleMeters;
		const float SolverHeightScale = static_cast<float>(FMath::Max(
			PhysicalElevationScaleMeters / FMath::Max(PhysicalSampleSpacingMeters, UE_DOUBLE_SMALL_NUMBER)
				* DomainRepresentativeCentimeters,
			1.0));

		FEonformScalarField BiasMask;
		const FEonformScalarField* EffectiveArea = nullptr;
		if (Settings.SelectiveProcessing != TEXT("None"))
		{
			if (!ErosionBuildBiasMask(Node, *Height, SolverHeightScale, AreaInput, BiasMask))
			{
				Error = TEXT("Erosion could not build its selective processing Area mask.");
				return false;
			}
			EffectiveArea = &BiasMask;
		}

		FEonformHydraulicErosionResult Result;
		if (!FEonformHydraulicErosion::Evaluate(
			*Height,
			SolverHeightScale,
			Settings,
			Result,
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::Rainfall),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::HydraulicErosion),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::Deposition),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::Evaporation),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::RockHardness),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::SoilDepth),
			EffectiveArea,
			nullptr))
		{
			Error = TEXT("Erosion evaluation failed.");
			return false;
		}

		FEonformScalarField WearOutput = Result.Wear;
		FEonformScalarField DepositsOutput = Result.Deposits;
		FEonformScalarField FlowOutput = Result.Flow;
		if (!PreparedDataset.SetScalarField(MoveTemp(Result.Height))
			|| !PreparedDataset.SetScalarField(MoveTemp(Result.Wear))
			|| !PreparedDataset.SetScalarField(MoveTemp(Result.Deposits))
			|| !PreparedDataset.SetScalarField(MoveTemp(Result.Flow)))
		{
			Error = TEXT("Erosion could not publish its terrain fields.");
			return false;
		}

		// The new Height invalidates analysis of the pre-erosion surface. Rebuild
		// only cheap intrinsic context here. Drainage remains demand-driven and is
		// derived later only when a downstream node actually requests hydrology.
		if (!FEonformTerrainDerivedData::EnsureContext(
			PreparedDataset,
			FMath::Max(Input->HeightScale, 1.0f),
			Context.PhysicalMetrics,
			&Error)) return false;

		FEonformTerrainValue TerrainResult = FEonformTerrainValue::MakeTerrain(MoveTemp(PreparedDataset), Input->HeightScale);
		if (!TerrainResult.IsValid())
		{
			Error = TEXT("Erosion produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(TerrainResult));
		Out.Outputs.Add(TEXT("Wear"), FEonformTerrainValue::MakeScalarField(MoveTemp(WearOutput)));
		Out.Outputs.Add(TEXT("Deposits"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepositsOutput)));
		Out.Outputs.Add(TEXT("Flow"), FEonformTerrainValue::MakeScalarField(MoveTemp(FlowOutput)));
		return true;
	}
}

void RegisterEonformErosionNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Descriptor.DisplayName = TEXT("Erosion");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Simulates hydraulic erosion with Gaea's current erosion, downcutting, scale, flow, and selective-processing controls.");
	Descriptor.Inputs.Add(ErosionTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Inputs.Add(ErosionScalarPort(TEXT("Area"), TEXT("Area")));
	Descriptor.Outputs.Add(ErosionTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(ErosionScalarPort(TEXT("Wear"), TEXT("Wear")));
	Descriptor.Outputs.Add(ErosionScalarPort(TEXT("Deposits"), TEXT("Deposits")));
	Descriptor.Outputs.Add(ErosionScalarPort(TEXT("Flow"), TEXT("Flow")));
	Descriptor.Parameters.Add(ErosionIntegerParameter(TEXT("Duration"), TEXT("Duration"), 24, 1, 4096, TEXT("Erosion")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("RockSoftness"), TEXT("Rock Softness"), 0.0, 0.0, 1.0, TEXT("Erosion")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Strength"), TEXT("Strength"), 1.0, 0.0, 4.0, TEXT("Erosion")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Downcutting"), TEXT("Downcutting"), 0.5, 0.0, 2.0, TEXT("Downcutting")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Inhibition"), TEXT("Inhibition"), 0.0, 0.0, 1.0, TEXT("Downcutting")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("BaseLevel"), TEXT("Base Level"), -1.0, -1.0, 1.0, TEXT("Downcutting")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("FeatureScale"), TEXT("Feature Scale"), 1.0, 0.25, 8.0, TEXT("Scale")));
	Descriptor.Parameters.Add(ErosionBooleanParameter(TEXT("RealScale"), TEXT("Real Scale"), true, TEXT("Scale")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("TerrainScale"), TEXT("Terrain Scale"), 1.0, 0.01, 100.0, TEXT("Scale")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Verticality"), TEXT("Verticality"), 1.0, 0.01, 10.0, TEXT("Scale")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Debris"), TEXT("Debris"), 0.5, 0.0, 1.0, TEXT("Flow")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Volume"), TEXT("Volume"), 1.0, 0.0, 4.0, TEXT("Flow")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Flow")));
	Descriptor.Parameters.Add(ErosionNameParameter(TEXT("AreaEffect"), TEXT("Area Effect"), TEXT("None"), { TEXT("Erosion Strength"), TEXT("Rock Softness"), TEXT("Precipitation Amount"), TEXT("None") }, TEXT("Selective Processing")));
	Descriptor.Parameters.Add(ErosionNameParameter(TEXT("BiasType"), TEXT("Bias Type"), TEXT("Altitude"), { TEXT("Altitude"), TEXT("Slope") }, TEXT("Selective Processing")));
	Descriptor.Parameters.Add(ErosionNumberParameter(TEXT("Bias"), TEXT("Bias"), 0.5, 0.0, 1.0, TEXT("Selective Processing")));
	Descriptor.Parameters.Add(ErosionBooleanParameter(TEXT("Reverse"), TEXT("Reverse"), false, TEXT("Selective Processing")));
	Descriptor.Parameters.Add(ErosionIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	Descriptor.Parameters.Add(ErosionBooleanParameter(TEXT("AggressiveMode"), TEXT("Aggressive Mode"), false));
	Descriptor.Parameters.Add(ErosionBooleanParameter(TEXT("Deterministic"), TEXT("Deterministic"), true));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::HydraulicErosion, EvaluateErosionNodeCurrent);
}
