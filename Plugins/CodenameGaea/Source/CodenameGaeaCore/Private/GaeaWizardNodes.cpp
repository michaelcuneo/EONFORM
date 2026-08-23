#include "GaeaWizardNodes.h"

#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosion.h"

namespace GaeaWizardNodes
{
	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = Max;
		P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Values, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Value : Values) P.NameOptions.Add(Value);
		P.Group = Group;
		return P;
	}

	FGaeaScalarField MakeScalar(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor D;
		D.Name = Name;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, D, 0.0f);
		return Field;
	}

	float Smooth01(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float Level(FName Value)
	{
		if (Value == TEXT("Low")) return 0.25f;
		if (Value == TEXT("High")) return 0.75f;
		if (Value == TEXT("Ultra")) return 1.0f;
		return 0.5f;
	}

	struct FWizardControls
	{
		FName Preset = TEXT("Balanced");
		float Strength = 0.58f;
		float ChannelDepth = 0.55f;
		float ChannelWidth = 0.48f;
		float Material = 0.45f;
		float Deposits = 0.5f;
		float Removal = 0.12f;
		float Bulk = 0.55f;
		float Rivers = 0.5f;
		float Furrows = 0.35f;
		float Talus = 0.28f;
		int32 Duration = 28;
		int32 Seed = 1337;
	};

	bool PublishResult(
		FGaeaTerrainDataset&& Dataset,
		float HeightScale,
		FGaeaHydraulicErosionResult&& Result,
		FGaeaScalarField&& ProcessMask,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FGaeaScalarField WearOutput = Result.Wear;
		FGaeaScalarField DepositsOutput = Result.Deposits;
		FGaeaScalarField FlowOutput = Result.Flow;
		FGaeaScalarField MaskOutput = ProcessMask;

		Result.Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Result.Height))
			|| !Dataset.SetScalarField(MoveTemp(Result.Wear))
			|| !Dataset.SetScalarField(MoveTemp(Result.Deposits))
			|| !Dataset.SetScalarField(MoveTemp(Result.Flow))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(ProcessMask)))
		{
			Error = TEXT("Wizard could not publish its terrain/process fields.");
			return false;
		}

		FGaeaTerrainValue Terrain = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Terrain.IsValid())
		{
			Error = TEXT("Wizard produced an invalid terrain output.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		Out.Outputs.Add(TEXT("Wear"), FGaeaTerrainValue::MakeScalarField(MoveTemp(WearOutput)));
		Out.Outputs.Add(TEXT("Deposits"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepositsOutput)));
		Out.Outputs.Add(TEXT("Flow"), FGaeaTerrainValue::MakeScalarField(MoveTemp(FlowOutput)));
		Out.Outputs.Add(TEXT("ProcessMask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(MaskOutput)));
		Error.Reset();
		return true;
	}

	bool EvaluateComposite(
		const FGaeaTerrainValue& Input,
		const FGaeaTerrainEvaluationContext& Context,
		const FWizardControls& C,
		FName ProcessMaskName,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FGaeaTerrainDataset Dataset = Input.TerrainDataset;
		FGaeaTerrainDerivedDataSettings DerivedSettings;
		DerivedSettings.GeologySeed = C.Seed;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(
			Dataset,
			Input.HeightScale,
			Context.PhysicalMetrics,
			DerivedSettings,
			&Error))
		{
			return false;
		}
		if (!FGaeaTerrainDerivedData::EnsureFlowAnalysis(
			Dataset,
			Input.HeightScale,
			Context.PhysicalMetrics,
			&Error))
		{
			return false;
		}

		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Mountain = Dataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
		const FGaeaScalarField* Plains = Dataset.FindScalarField(GaeaTerrainFieldNames::Plains);
		const FGaeaScalarField* Rainfall = Dataset.FindScalarField(GaeaTerrainFieldNames::Rainfall);
		const FGaeaScalarField* Catchment = Dataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2);
		const FGaeaScalarField* Hardness = Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness);
		const FGaeaScalarField* SoilDepth = Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
		const FGaeaScalarField* ThermalMask = Dataset.FindScalarField(GaeaTerrainFieldNames::Thermal);
		if (!Height || !Slope || !Concavity || !Mountain || !Plains || !Rainfall || !Catchment || !Hardness || !SoilDepth)
		{
			Error = TEXT("Wizard could not resolve the EONFORM context, geology, and flow-analysis fields it requires.");
			return false;
		}

		float MaxCatchment = 0.0f;
		for (const float Value : Catchment->Values) MaxCatchment = FMath::Max(MaxCatchment, Value);
		const float CatchmentDenominator = FMath::Max(FMath::Loge(1.0f + MaxCatchment), UE_SMALL_NUMBER);

		FGaeaScalarField ProcessMask = MakeScalar(Height->Domain, ProcessMaskName);
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				const float Slope01 = Smooth01(Slope->AtInterior(X, Y) / 42.0f);
				const float Valley = FMath::Clamp(Concavity->AtInterior(X, Y), 0.0f, 1.0f);
				const float Highland = FMath::Clamp(Mountain->AtInterior(X, Y), 0.0f, 1.0f);
				const float Lowland = FMath::Clamp(Plains->AtInterior(X, Y), 0.0f, 1.0f);
				const float Rain = FMath::Clamp(Rainfall->AtInterior(X, Y), 0.0f, 1.0f);
				const float SoftRock = 1.0f - FMath::Clamp(Hardness->AtInterior(X, Y), 0.0f, 1.0f);
				const float Catchment01 = FMath::Clamp(FMath::Loge(1.0f + Catchment->AtInterior(X, Y)) / CatchmentDenominator, 0.0f, 1.0f);

				float Regional = 0.0f;
				if (C.Preset == TEXT("MountainWeathering"))
				{
					Regional = Slope01 * 0.42f + Highland * 0.33f + SoftRock * 0.15f + Rain * 0.10f;
				}
				else if (C.Preset == TEXT("RiverCarving"))
				{
					Regional = Catchment01 * 0.55f + Valley * 0.25f + Rain * 0.15f + Slope01 * 0.05f;
				}
				else if (C.Preset == TEXT("SoftSediment"))
				{
					Regional = SoftRock * 0.35f + Lowland * 0.30f + Valley * 0.20f + Catchment01 * 0.15f;
				}
				else if (C.Preset == TEXT("Harsh"))
				{
					Regional = Slope01 * 0.30f + Catchment01 * 0.30f + Highland * 0.20f + Rain * 0.20f;
				}
				else
				{
					Regional = Slope01 * 0.24f + Catchment01 * 0.28f + Valley * 0.16f + Highland * 0.12f + Rain * 0.12f + SoftRock * 0.08f;
				}

				const float RiverAccent = Catchment01 * C.Rivers;
				const float FurrowAccent = Slope01 * Rain * C.Furrows;
				ProcessMask.AtInterior(X, Y) = FMath::Clamp(Regional * 0.72f + RiverAccent * 0.18f + FurrowAccent * 0.10f, 0.0f, 1.0f);
			}
		}

		FGaeaHydraulicErosionSettings Settings;
		Settings.Iterations = FMath::Clamp(C.Duration, 1, 4096);
		Settings.Strength = FMath::Clamp(FMath::Lerp(0.25f, 2.5f, C.Strength), 0.0f, 4.0f);
		Settings.Downcutting = FMath::Clamp(FMath::Lerp(0.15f, 1.65f, C.ChannelDepth) * FMath::Lerp(0.75f, 1.25f, C.Rivers), 0.0f, 2.0f);
		Settings.FeatureScale = FMath::Clamp(FMath::Lerp(2.6f, 0.45f, C.ChannelWidth), 0.25f, 8.0f);
		Settings.RockSoftness = FMath::Clamp(C.Material, 0.0f, 1.0f);
		Settings.Debris = FMath::Clamp(C.Deposits, 0.0f, 1.0f);
		Settings.Volume = FMath::Clamp(FMath::Lerp(0.65f, 2.5f, C.Bulk), 0.0f, 4.0f);
		Settings.SedimentRemoval = FMath::Clamp(C.Removal, 0.0f, 1.0f);
		Settings.SelectiveProcessing = TEXT("ErosionStrength");
		Settings.Seed = C.Seed;
		Settings.bDeterministic = true;

		const FVector2d SampleSpacing = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Height->Domain.Dimensions, Height->Domain.GetCellSize());
		Settings.PhysicalSampleSpacingMeters = FMath::Min(SampleSpacing.X, SampleSpacing.Y);
		Settings.PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input.HeightScale);

		FGaeaHydraulicErosionResult Result;
		if (!FGaeaHydraulicErosion::Evaluate(
			*Height,
			FMath::Max(Input.HeightScale, 1.0f),
			Settings,
			Result,
			Rainfall,
			Dataset.FindScalarField(GaeaTerrainFieldNames::HydraulicErosion),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition),
			Dataset.FindScalarField(GaeaTerrainFieldNames::Evaporation),
			Hardness,
			SoilDepth,
			&ProcessMask,
			nullptr))
		{
			Error = TEXT("Wizard hydraulic erosion pass failed.");
			return false;
		}

		if (C.Talus > UE_SMALL_NUMBER)
		{
			FGaeaThermalErosionSettings Thermal;
			Thermal.Iterations = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(2.0f, 18.0f, C.Talus)), 1, 64);
			Thermal.TalusAngleDegrees = FMath::Lerp(40.0f, 27.0f, C.Talus);
			Thermal.Strength = FMath::Clamp(FMath::Lerp(0.08f, 0.6f, C.Talus), 0.0f, 1.0f);
			if (!FGaeaThermalErosion::ApplyInPlace(
				Result.Height,
				FMath::Max(Input.HeightScale, 1.0f),
				Thermal,
				ThermalMask,
				Hardness,
				nullptr,
				&Error))
			{
				return false;
			}
		}

		return PublishResult(MoveTemp(Dataset), Input.HeightScale, MoveTemp(Result), MoveTemp(ProcessMask), Out, Error);
	}

	bool EvaluateWizard(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Wizard requires a valid terrain input 'Terrain'.");
			return false;
		}

		FWizardControls C;
		C.Preset = Node.GetName(TEXT("Preset"), TEXT("Balanced"));
		C.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), C.Strength)), 0.0f, 1.0f);
		C.ChannelDepth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Depth"), C.ChannelDepth)), 0.0f, 1.0f);
		C.ChannelWidth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Width"), C.ChannelWidth)), 0.0f, 1.0f);
		C.Material = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Material"), C.Material)), 0.0f, 1.0f);
		C.Deposits = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Deposits"), C.Deposits)), 0.0f, 1.0f);
		C.Removal = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Removal"), C.Removal)), 0.0f, 1.0f);
		C.Bulk = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Bulk"), C.Bulk)), 0.0f, 1.0f);
		C.Rivers = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Rivers"), C.Rivers)), 0.0f, 1.0f);
		C.Furrows = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Furrows"), C.Furrows)), 0.0f, 1.0f);
		C.Talus = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Talus"), C.Talus)), 0.0f, 1.0f);
		C.Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), C.Duration)), 1, 4096);
		C.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), C.Seed));
		return EvaluateComposite(*Input, Context, C, TEXT("WizardProcessMask"), Out, Error);
	}

	bool EvaluateWizard2(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Wizard2 requires a valid terrain input 'Terrain'.");
			return false;
		}

		const float Power = Level(Node.GetName(TEXT("Power"), TEXT("Med")));
		const float Depth = Level(Node.GetName(TEXT("Depth"), TEXT("Med")));
		const float Scale = Level(Node.GetName(TEXT("Scale"), TEXT("Med")));
		const float Deposits = Level(Node.GetName(TEXT("Deposits"), TEXT("Med")));
		const float Flow = Level(Node.GetName(TEXT("Flow"), TEXT("Med")));
		const float Shape = Level(Node.GetName(TEXT("Shape"), TEXT("Med")));
		const float Detail = Level(Node.GetName(TEXT("Detail"), TEXT("Med")));

		FWizardControls C;
		C.Preset = Shape >= 0.875f ? TEXT("Harsh") : Shape >= 0.625f ? TEXT("MountainWeathering") : Shape <= 0.375f ? TEXT("SoftSediment") : TEXT("Balanced");
		C.Strength = Power;
		C.ChannelDepth = Depth;
		C.ChannelWidth = 1.0f - Scale * 0.72f;
		C.Material = FMath::Clamp(0.25f + Shape * 0.5f, 0.0f, 1.0f);
		C.Deposits = Deposits;
		C.Removal = FMath::Clamp((1.0f - Deposits) * 0.35f, 0.0f, 1.0f);
		C.Bulk = FMath::Clamp(0.35f + Deposits * 0.55f, 0.0f, 1.0f);
		C.Rivers = Flow;
		C.Furrows = Detail;
		C.Talus = FMath::Clamp((Shape * 0.55f + Detail * 0.45f) * 0.7f, 0.0f, 1.0f);
		C.Duration = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(10.0f, 56.0f, Power * 0.65f + Detail * 0.35f)), 1, 4096);
		C.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		return EvaluateComposite(*Input, Context, C, TEXT("Wizard2ProcessMask"), Out, Error);
	}
}

void RegisterGaeaWizardNodes()
{
	using namespace GaeaWizardNodes;

	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Wizard;
		D.DisplayName = TEXT("Wizard");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Context-aware erosion orchestrator. Curated controls translate into regional hydraulic, sediment, river, furrow, and talus behavior using EONFORM geology and hydrology rather than applying one erosion strength everywhere.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Wear"), TEXT("Wear")));
		D.Outputs.Add(ScalarPort(TEXT("Deposits"), TEXT("Deposits")));
		D.Outputs.Add(ScalarPort(TEXT("Flow"), TEXT("Flow")));
		D.Outputs.Add(ScalarPort(TEXT("ProcessMask"), TEXT("Process Mask")));
		D.Parameters.Add(Choice(TEXT("Preset"), TEXT("Preset"), TEXT("Balanced"), { TEXT("Balanced"), TEXT("MountainWeathering"), TEXT("RiverCarving"), TEXT("SoftSediment"), TEXT("Harsh") }, TEXT("Recipe")));
		D.Parameters.Add(Num(TEXT("Strength"), TEXT("Strength"), 0.58, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Depth"), TEXT("Depth"), 0.55, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Width"), TEXT("Width"), 0.48, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Material"), TEXT("Material"), 0.45, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Deposits"), TEXT("Deposits"), 0.50, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Removal"), TEXT("Removal"), 0.12, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Bulk"), TEXT("Bulk"), 0.55, 0.0, 1.0, TEXT("Phase 1")));
		D.Parameters.Add(Num(TEXT("Rivers"), TEXT("Rivers"), 0.50, 0.0, 1.0, TEXT("Phase 2")));
		D.Parameters.Add(Num(TEXT("Furrows"), TEXT("Furrows"), 0.35, 0.0, 1.0, TEXT("Phase 2")));
		D.Parameters.Add(Num(TEXT("Talus"), TEXT("Talus"), 0.28, 0.0, 1.0, TEXT("Phase 2")));
		D.Parameters.Add(Int(TEXT("Duration"), TEXT("Duration"), 28, 1, 4096, TEXT("Simulation")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Simulation")));
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateWizard);
	}

	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = GaeaTerrainNodeTypes::Wizard2;
		D.DisplayName = TEXT("Wizard2");
		D.Category = TEXT("Simulate");
		D.Description = TEXT("Simplified erosion art direction. Low/Med/High/Ultra controls are translated into the same context-aware EONFORM erosion recipe used by Wizard.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Wear"), TEXT("Wear")));
		D.Outputs.Add(ScalarPort(TEXT("Deposits"), TEXT("Deposits")));
		D.Outputs.Add(ScalarPort(TEXT("Flow"), TEXT("Flow")));
		D.Outputs.Add(ScalarPort(TEXT("ProcessMask"), TEXT("Process Mask")));
		const std::initializer_list<FName> Levels = { TEXT("Low"), TEXT("Med"), TEXT("High"), TEXT("Ultra") };
		D.Parameters.Add(Choice(TEXT("Power"), TEXT("Power"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Choice(TEXT("Depth"), TEXT("Depth"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Choice(TEXT("Scale"), TEXT("Scale"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Choice(TEXT("Deposits"), TEXT("Deposits"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Choice(TEXT("Flow"), TEXT("Flow"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Choice(TEXT("Shape"), TEXT("Shape"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Choice(TEXT("Detail"), TEXT("Detail"), TEXT("Med"), Levels, TEXT("Art Direction")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Simulation")));
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateWizard2);
	}
}