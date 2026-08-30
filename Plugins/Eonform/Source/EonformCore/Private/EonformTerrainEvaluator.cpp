#include "EonformTerrainEvaluator.h"

#include "EonformGridDomain.h"
#include "EonformHydraulicErosion.h"
#include "EonformTerrainContext.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainDomainScaling.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainGeology.h"
#include "EonformTerrainNoise.h"
#include "EonformTerrainShaping.h"

namespace
{
	FCriticalSection RegistryMutex;
	TMap<FName, FEonformTerrainNodeEvaluator> Registry;

	const FEonformTerrainValue* FindInput(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* Value = Inputs.Find(Name);
		return Value ? *Value : nullptr;
	}

	const FEonformTerrainValue* RequireTerrainInput(
		const FEonformTerrainNodeInputs& Inputs,
		FName Name,
		const TCHAR* NodeName,
		FString& Error)
	{
		const FEonformTerrainValue* Value = FindInput(Inputs, Name);
		if (!Value || Value->Type != EEonformTerrainValueType::Terrain || !Value->IsValid())
		{
			Error = FString::Printf(TEXT("%s requires a terrain input '%s'."), NodeName, *Name.ToString());
			return nullptr;
		}
		return Value;
	}

	const FEonformScalarField* OptionalScalarInput(
		const FEonformTerrainNodeInputs& Inputs,
		FName Name,
		const FEonformGridDomain& Domain,
		FString& Error)
	{
		const FEonformTerrainValue* Value = FindInput(Inputs, Name);
		if (!Value) return nullptr;
		if (Value->Type != EEonformTerrainValueType::ScalarField || !Value->ScalarField.IsValid())
		{
			Error = FString::Printf(TEXT("Input '%s' must be a valid scalar field."), *Name.ToString());
			return nullptr;
		}
		if (Value->ScalarField.Domain != Domain)
		{
			Error = FString::Printf(TEXT("Input '%s' must use the same domain as Height."), *Name.ToString());
			return nullptr;
		}
		return &Value->ScalarField;
	}

	bool PublishTerrain(
		FEonformTerrainNodeEvaluation& Out,
		FEonformTerrainDataset&& Dataset,
		float HeightScale,
		FString& Error)
	{
		FEonformTerrainValue Value = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Terrain node produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Terrain"), MoveTemp(Value));
		return true;
	}

	bool BuildFlatTerrainResult(const FEonformTerrainEvaluationContext& Context, FEonformTerrainEvaluationResult& Result)
	{
		const int32 Width = Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257;
		const int32 HeightSamples = Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : Width;
		double WorldWidth = 100000.0;
		double WorldDepth = 100000.0;
		float HeightScale = 8000.0f;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WorldWidth = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			WorldDepth = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		if (Context.PhysicalMetrics.HasElevationScale())
		{
			HeightScale = static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		}

		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(Width, HeightSamples),
			FVector2d(-WorldWidth * 0.5, -WorldDepth * 0.5),
			FVector2d(WorldWidth * 0.5, WorldDepth * 0.5));
		if (!Domain.IsValid())
		{
			Result.Error = TEXT("Could not create the default flat terrain domain.");
			return false;
		}

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;

		FEonformScalarField Height;
		Height.Initialize(Domain, Descriptor);
		if (!Height.IsValid() || !Result.Dataset.SetScalarField(MoveTemp(Height)))
		{
			Result.Error = TEXT("Could not create the default flat Height field.");
			return false;
		}

		Result.HeightScale = HeightScale;
		Result.bSuccess = true;
		Result.Error.Reset();
		return true;
	}

	bool EvaluateSourceDataset(
		const FEonformTerrainNode&,
		const FEonformTerrainNodeInputs&,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		if (Context.SourceDataset.IsEmpty())
		{
			Error = TEXT("SourceDataset node received an empty source dataset.");
			return false;
		}
		FEonformTerrainDataset Dataset = Context.SourceDataset;
		return PublishTerrain(Out, MoveTemp(Dataset), FMath::Max(Context.HeightScale, 1.0f), Error);
	}

	bool EvaluatePerlinNoise(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs&,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const int32 LegacyResolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const int32 Width = Context.TargetResolution.X > 1 ? Context.TargetResolution.X : LegacyResolution;
		const int32 HeightSamples = Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : Width;
		const float LegacyWorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		double WorldWidth = LegacyWorldSize;
		double WorldDepth = LegacyWorldSize;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WorldWidth = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			WorldDepth = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		const float HeightScale = Context.PhysicalMetrics.HasElevationScale()
			? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0)
			: FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const int32 Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), 1337), MIN_int32, MAX_int32));

		FEonformFractalNoiseSettings Settings;
		Settings.Frequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Frequency"), Settings.Frequency)), 0.000001f, 1.0f);
		Settings.Octaves = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), Settings.Octaves)), 1, 16);
		Settings.Persistence = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Persistence"), Settings.Persistence)), 0.0f, 1.0f);
		Settings.Lacunarity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Lacunarity"), Settings.Lacunarity)), 1.0f, 8.0f);

		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(Width, HeightSamples),
			FVector2d(-WorldWidth * 0.5, -WorldDepth * 0.5),
			FVector2d(WorldWidth * 0.5, WorldDepth * 0.5));
		if (!Domain.IsValid())
		{
			Error = TEXT("PerlinNoise produced an invalid grid domain.");
			return false;
		}

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;

		FEonformScalarField Height;
		Height.Initialize(Domain, Descriptor);
		const FVector2D SeedOffset = FEonformTerrainNoise::MakeSeedOffset(Seed);
		for (int32 Y = 0; Y < HeightSamples; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				Height.AtInterior(X, Y) = FEonformTerrainNoise::SampleFractal(
					FVector2D(static_cast<float>(World.X), static_cast<float>(World.Y)),
					SeedOffset,
					Settings);
			}
		}

		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("PerlinNoise could not publish its Height field.");
			return false;
		}
		return PublishTerrain(Out, MoveTemp(Dataset), HeightScale, Error);
	}

	bool EvaluateTerrainShapeNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("TerrainShape"), Error);
		if (!Input) return false;
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("TerrainShape input dataset has no Height field.");
			return false;
		}
		FEonformTerrainShapeSettings Settings;
		Settings.Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), Settings.Seed), MIN_int32, MAX_int32));
		Settings.MacroStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MacroStrength"), Settings.MacroStrength)), 0.0f, 2.0f);
		Settings.MountainThreshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MountainThreshold"), Settings.MountainThreshold)), -1.0f, 1.0f);
		Settings.WarpStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpStrength"), Settings.WarpStrength)), 0.0f, 25000.0f);
		Settings.RidgeStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RidgeStrength"), Settings.RidgeStrength)), 0.0f, 2.0f);
		Settings.FoothillStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FoothillStrength"), Settings.FoothillStrength)), 0.0f, 1.0f);
		Settings.ValleyDepth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ValleyDepth"), Settings.ValleyDepth)), 0.0f, 1.0f);
		Settings.PlainsStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("PlainsStrength"), Settings.PlainsStrength)), 0.0f, 1.0f);
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!FEonformTerrainShaping::Apply(*Height, Settings, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateTerrainContextNode(
		const FEonformTerrainNode&,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("TerrainContext"), Error);
		if (!Input) return false;
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("TerrainContext input dataset has no Height field.");
			return false;
		}
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!FEonformTerrainContext::Analyze(*Height, FMath::Max(Input->HeightScale, 1.0f), Context.PhysicalMetrics, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateGeologyNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("Geology"), Error);
		if (!Input) return false;
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("Geology input dataset has no Height field.");
			return false;
		}
		FEonformTerrainGeologySettings Settings;
		const int32 Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), 1337), MIN_int32, MAX_int32));
		Settings.Frequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Frequency"), Settings.Frequency)), 0.000001f, 1.0f);
		Settings.Octaves = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), Settings.Octaves)), 1, 16);
		Settings.Contrast = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Contrast"), Settings.Contrast)), 0.1f, 4.0f);
		Settings.MountainHardnessBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MountainHardnessBias"), Settings.MountainHardnessBias)), 0.0f, 1.0f);
		Settings.PlainsSoftnessBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("PlainsSoftnessBias"), Settings.PlainsSoftnessBias)), 0.0f, 1.0f);
		Settings.SoilFormationStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SoilFormationStrength"), Settings.SoilFormationStrength)), 0.0f, 2.0f);
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!FEonformTerrainGeology::Build(*Height, Seed, Settings, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateProcessMasksNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext&,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("ProcessMasks"), Error);
		if (!Input) return false;
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("ProcessMasks input dataset has no Height field.");
			return false;
		}
		FEonformTerrainProcessMaskSettings Settings;
		Settings.ThermalTalusAngleDegrees = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ThermalTalusAngle"), Settings.ThermalTalusAngleDegrees)), 0.0f, 90.0f);
		Settings.ThermalRegionality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ThermalRegionality"), Settings.ThermalRegionality)), 0.0f, 1.0f);
		Settings.HydraulicRegionality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HydraulicRegionality"), Settings.HydraulicRegionality)), 0.0f, 1.0f);
		Settings.RainfallHighlandBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RainfallHighlandBias"), Settings.RainfallHighlandBias)), 0.0f, 1.0f);
		Settings.EvaporationLowlandBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("EvaporationLowlandBias"), Settings.EvaporationLowlandBias)), 0.0f, 1.0f);
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!FEonformTerrainContext::BuildProcessMasks(*Height, Settings, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateSlopeNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("Slope"), Error);
		if (!Input) return false;
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Slope input terrain has no valid Height field.");
			return false;
		}
		const float MinSlope = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Min"), 0.0)), 0.0f, 90.0f);
		const float MaxSlope = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Max"), 45.0)), MinSlope, 90.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 5.0)), 0.0f, 45.0f);
		const FIntPoint Dimensions = Height->Domain.Dimensions;
		const FVector2d CellSizeMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(Dimensions, Height->Domain.GetCellSize());
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		if (CellSizeMeters.X <= UE_DOUBLE_SMALL_NUMBER || CellSizeMeters.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Slope input terrain has an invalid physical grid spacing.");
			return false;
		}
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("SlopeMask");
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Mask;
		Mask.Initialize(Height->Domain, Descriptor);
		for (int32 Y = 0; Y < Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Dimensions.X; ++X)
			{
				const int32 XL = FMath::Max(0, X - 1);
				const int32 XR = FMath::Min(Dimensions.X - 1, X + 1);
				const int32 YD = FMath::Max(0, Y - 1);
				const int32 YU = FMath::Min(Dimensions.Y - 1, Y + 1);
				const double DX = static_cast<double>(Height->AtInterior(XR, Y) - Height->AtInterior(XL, Y)) * ElevationScaleMeters
					/ FMath::Max(static_cast<double>(XR - XL) * CellSizeMeters.X, UE_DOUBLE_SMALL_NUMBER);
				const double DY = static_cast<double>(Height->AtInterior(X, YU) - Height->AtInterior(X, YD)) * ElevationScaleMeters
					/ FMath::Max(static_cast<double>(YU - YD) * CellSizeMeters.Y, UE_DOUBLE_SMALL_NUMBER);
				const float SlopeDegrees = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY))));
				float Weight = 0.0f;
				if (SlopeDegrees >= MinSlope && SlopeDegrees <= MaxSlope) Weight = 1.0f;
				else if (SlopeDegrees > MaxSlope && Falloff > UE_SMALL_NUMBER && SlopeDegrees < MaxSlope + Falloff)
				{
					const float T = FMath::Clamp((SlopeDegrees - MaxSlope) / Falloff, 0.0f, 1.0f);
					const float Smooth = T * T * (3.0f - 2.0f * T);
					Weight = 1.0f - Smooth;
				}
				Mask.AtInterior(X, Y) = Weight;
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

	bool EvaluateHydraulicErosionNode(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("Erosion"), Error);
		if (!Input) return false;
		FEonformTerrainDataset PreparedDataset = Input->TerrainDataset;
		FEonformTerrainDerivedDataSettings DerivedSettings;
		if (!FEonformTerrainDerivedData::EnsureHydraulicInputs(PreparedDataset, FMath::Max(Input->HeightScale, 1.0f), Context.PhysicalMetrics, DerivedSettings, &Error)) return false;
		const FEonformScalarField* Height = PreparedDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("Erosion input dataset has no Height field.");
			return false;
		}
		const bool bHasAreaInput = FindInput(Inputs, TEXT("Mask")) != nullptr;
		const FEonformScalarField* AreaMask = OptionalScalarInput(Inputs, TEXT("Mask"), Height->Domain, Error);
		if (bHasAreaInput && !AreaMask) return false;
		const bool bHasSedimentInput = FindInput(Inputs, TEXT("Sediment")) != nullptr;
		const FEonformScalarField* SedimentInput = OptionalScalarInput(Inputs, TEXT("Sediment"), Height->Domain, Error);
		if (bHasSedimentInput && !SedimentInput) return false;
		FEonformHydraulicErosionSettings Settings;
		Settings.Iterations = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), Settings.Iterations)), 1, 4096);
		Settings.RockSoftness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RockSoftness"), Settings.RockSoftness)), 0.0f, 1.0f);
		Settings.Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), Settings.Strength)), 0.0f, 4.0f);
		Settings.Downcutting = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Downcutting"), Settings.Downcutting)), 0.0f, 2.0f);
		Settings.Inhibition = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Inhibition"), Settings.Inhibition)), 0.0f, 1.0f);
		Settings.BaseLevel = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("BaseLevel"), Settings.BaseLevel)), -1.0f, 1.0f);
		Settings.FeatureScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FeatureScale"), Settings.FeatureScale)), 0.25f, 8.0f);
		Settings.Debris = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Debris"), Settings.Debris)), 0.0f, 1.0f);
		Settings.Volume = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Volume"), Settings.Volume)), 0.0f, 4.0f);
		Settings.SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), Settings.SedimentRemoval)), 0.0f, 1.0f);
		Settings.SelectiveProcessing = Node.GetName(TEXT("SelectiveProcessing"), Settings.SelectiveProcessing);
		Settings.Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), Settings.Seed), MIN_int32, MAX_int32));
		Settings.bAggressiveMode = Node.GetBool(TEXT("AggressiveMode"), Settings.bAggressiveMode);
		Settings.bDeterministic = Node.GetBool(TEXT("Deterministic"), Settings.bDeterministic);
		const FVector2d DomainCellSize = Height->Domain.GetCellSize();
		const double DomainRepresentativeCentimeters = FMath::Max(FMath::Min(FMath::Abs(DomainCellSize.X), FMath::Abs(DomainCellSize.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSampleSpacingMeters = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height->Domain.Dimensions, DomainCellSize);
		const double PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		Settings.PhysicalSampleSpacingMeters = PhysicalSampleSpacingMeters;
		Settings.PhysicalElevationScaleMeters = PhysicalElevationScaleMeters;
		const float SolverHeightScale = static_cast<float>(FMath::Max(
			PhysicalElevationScaleMeters / FMath::Max(PhysicalSampleSpacingMeters, UE_DOUBLE_SMALL_NUMBER) * DomainRepresentativeCentimeters,
			1.0));
		FEonformHydraulicErosionResult Result;
		if (!FEonformHydraulicErosion::Evaluate(
			*Height, SolverHeightScale, Settings, Result,
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::Rainfall),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::HydraulicErosion),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::Deposition),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::Evaporation),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::RockHardness),
			PreparedDataset.FindScalarField(EonformTerrainFieldNames::SoilDepth),
			AreaMask, SedimentInput))
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
		if (!FEonformTerrainDerivedData::EnsureContext(PreparedDataset, FMath::Max(Input->HeightScale, 1.0f), Context.PhysicalMetrics, &Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureHydrology(PreparedDataset, FMath::Max(Input->HeightScale, 1.0f), Context.PhysicalMetrics, &Error)) return false;
		if (!PublishTerrain(Out, MoveTemp(PreparedDataset), Input->HeightScale, Error)) return false;
		Out.Outputs.Add(TEXT("Wear"), FEonformTerrainValue::MakeScalarField(MoveTemp(WearOutput)));
		Out.Outputs.Add(TEXT("Deposits"), FEonformTerrainValue::MakeScalarField(MoveTemp(DepositsOutput)));
		Out.Outputs.Add(TEXT("Flow"), FEonformTerrainValue::MakeScalarField(MoveTemp(FlowOutput)));
		return true;
	}
}

void FEonformTerrainNodeRegistry::Register(FName NodeType, FEonformTerrainNodeEvaluator Evaluator)
{
	if (NodeType.IsNone() || !Evaluator) return;

	// Terrain Output is authoritative. Legacy Gaea-facing source nodes often read
	// an authored integer parameter named Resolution. When an output resolution is
	// selected, override that legacy working resolution before the node evaluates
	// so procedural detail is regenerated at the requested grid instead of merely
	// being enlarged after the fact. Nodes that do not read Resolution are unchanged.
	FEonformTerrainNodeEvaluator Inner = MoveTemp(Evaluator);
	FEonformTerrainNodeEvaluator Wrapped = [Inner = MoveTemp(Inner)](
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error) mutable -> bool
	{
		if (Context.TargetResolution.X > 1)
		{
			FEonformTerrainNode EffectiveNode = Node;
			EffectiveNode.IntegerParameters.Add(TEXT("Resolution"), Context.TargetResolution.X);
			return Inner(EffectiveNode, Inputs, Context, Out, Error);
		}
		return Inner(Node, Inputs, Context, Out, Error);
	};

	FScopeLock Lock(&RegistryMutex);
	Registry.Add(NodeType, MoveTemp(Wrapped));
}

bool FEonformTerrainNodeRegistry::IsRegistered(FName NodeType)
{
	FScopeLock Lock(&RegistryMutex);
	return Registry.Contains(NodeType);
}

void FEonformTerrainNodeRegistry::RegisterBuiltIns()
{
	FScopeLock Lock(&RegistryMutex);
	if (!Registry.Contains(EonformTerrainNodeTypes::SourceDataset)) Registry.Add(EonformTerrainNodeTypes::SourceDataset, EvaluateSourceDataset);
	if (!Registry.Contains(EonformTerrainNodeTypes::PerlinNoise)) Registry.Add(EonformTerrainNodeTypes::PerlinNoise, EvaluatePerlinNoise);
	if (!Registry.Contains(EonformTerrainNodeTypes::TerrainShape)) Registry.Add(EonformTerrainNodeTypes::TerrainShape, EvaluateTerrainShapeNode);
	if (!Registry.Contains(EonformTerrainNodeTypes::TerrainContext)) Registry.Add(EonformTerrainNodeTypes::TerrainContext, EvaluateTerrainContextNode);
	if (!Registry.Contains(EonformTerrainNodeTypes::Geology)) Registry.Add(EonformTerrainNodeTypes::Geology, EvaluateGeologyNode);
	if (!Registry.Contains(EonformTerrainNodeTypes::ProcessMasks)) Registry.Add(EonformTerrainNodeTypes::ProcessMasks, EvaluateProcessMasksNode);
	if (!Registry.Contains(EonformTerrainNodeTypes::Slope)) Registry.Add(EonformTerrainNodeTypes::Slope, EvaluateSlopeNode);
	if (!Registry.Contains(EonformTerrainNodeTypes::HydraulicErosion)) Registry.Add(EonformTerrainNodeTypes::HydraulicErosion, EvaluateHydraulicErosionNode);
}

void FEonformTerrainNodeRegistry::Reset()
{
	FScopeLock Lock(&RegistryMutex);
	Registry.Reset();
}

const FEonformTerrainNodeEvaluator* FEonformTerrainNodeRegistry::Find(FName NodeType)
{
	return Registry.Find(NodeType);
}

bool FEonformTerrainEvaluator::EvaluateOutput(
	const FEonformTerrainRecipe& Recipe,
	const FEonformTerrainEvaluationContext& Context,
	const FGuid& NodeId,
	FName OutputName,
	FEonformTerrainValue& OutValue,
	FString* OutError)
{
	FString Error;
	if (!Recipe.Validate(&Error))
	{
		if (OutError) *OutError = Error;
		return false;
	}
	if (!NodeId.IsValid() || !Recipe.FindNode(NodeId))
	{
		if (OutError) *OutError = TEXT("Evaluator could not resolve the requested output node id.");
		return false;
	}

	FEonformTerrainNodeRegistry::RegisterBuiltIns();
	FEonformTerrainEvaluationContext EffectiveContext = Context;
	EffectiveContext.ActiveRecipe = &Recipe;

	TMap<FGuid, FEonformTerrainNodeEvaluation> Cache;
	TSet<FGuid> Visiting;
	TFunction<bool(const FGuid&)> EvaluateNode = [&](const FGuid& CurrentNodeId) -> bool
	{
		if (Cache.Contains(CurrentNodeId)) return true;
		if (Visiting.Contains(CurrentNodeId))
		{
			Error = TEXT("Terrain recipe contains a dependency cycle.");
			return false;
		}
		const FEonformTerrainNode* Node = Recipe.FindNode(CurrentNodeId);
		if (!Node)
		{
			Error = TEXT("Evaluator could not resolve a node id.");
			return false;
		}

		Visiting.Add(CurrentNodeId);
		FEonformTerrainNodeInputs RawInputs;
		for (const FEonformTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode != CurrentNodeId) continue;
			if (!EvaluateNode(Connection.FromNode))
			{
				Visiting.Remove(CurrentNodeId);
				return false;
			}
			const FEonformTerrainNodeEvaluation* SourceEvaluation = Cache.Find(Connection.FromNode);
			const FEonformTerrainValue* SourceOutput = SourceEvaluation ? SourceEvaluation->FindOutput(Connection.FromOutput) : nullptr;
			if (!SourceOutput || !SourceOutput->IsValid())
			{
				Error = FString::Printf(TEXT("Node connection references missing or invalid output '%s'."), *Connection.FromOutput.ToString());
				Visiting.Remove(CurrentNodeId);
				return false;
			}
			RawInputs.Add(Connection.ToInput, SourceOutput);
		}

		TMap<FName, FEonformTerrainValue> OwnedInputs;
		FEonformTerrainNodeInputs Inputs;
		if (!EonformTerrainDomainScaling::NormalizeInputs(RawInputs, EffectiveContext, OwnedInputs, Inputs, &Error))
		{
			Visiting.Remove(CurrentNodeId);
			return false;
		}

		FEonformTerrainNodeEvaluator Evaluator;
		{
			FScopeLock Lock(&RegistryMutex);
			const FEonformTerrainNodeEvaluator* Found = FEonformTerrainNodeRegistry::Find(Node->Type);
			if (!Found)
			{
				Error = FString::Printf(TEXT("No evaluator registered for node type '%s'."), *Node->Type.ToString());
				Visiting.Remove(CurrentNodeId);
				return false;
			}
			Evaluator = *Found;
		}

		FEonformTerrainNodeEvaluation NodeResult;
		if (!Evaluator(*Node, Inputs, EffectiveContext, NodeResult, Error))
		{
			Visiting.Remove(CurrentNodeId);
			return false;
		}
		if (NodeResult.Outputs.IsEmpty())
		{
			Error = FString::Printf(TEXT("Node '%s' produced no outputs."), *Node->Type.ToString());
			Visiting.Remove(CurrentNodeId);
			return false;
		}
		if (!EonformTerrainDomainScaling::NormalizeOutputs(NodeResult, EffectiveContext, &Error))
		{
			Visiting.Remove(CurrentNodeId);
			return false;
		}
		Cache.Add(CurrentNodeId, MoveTemp(NodeResult));
		Visiting.Remove(CurrentNodeId);
		return true;
	};

	if (!EvaluateNode(NodeId))
	{
		if (OutError) *OutError = Error;
		return false;
	}
	FEonformTerrainNodeEvaluation* Evaluation = Cache.Find(NodeId);
	FEonformTerrainValue* Value = Evaluation ? Evaluation->FindOutput(OutputName) : nullptr;
	if (!Value || !Value->IsValid())
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Requested output '%s' is missing or invalid."), *OutputName.ToString());
		}
		return false;
	}
	OutValue = MoveTemp(*Value);
	if (OutError) OutError->Reset();
	return true;
}

FEonformTerrainEvaluationResult FEonformTerrainEvaluator::Evaluate(
	const FEonformTerrainRecipe& Recipe,
	const FEonformTerrainEvaluationContext& Context)
{
	FEonformTerrainEvaluationResult Result;
	Result.RecipeHash = Recipe.GetDeterministicHash();
	if (!Recipe.Validate(&Result.Error)) return Result;
	if (Recipe.Nodes.IsEmpty())
	{
		BuildFlatTerrainResult(Context, Result);
		return Result;
	}

	FEonformTerrainValue TerrainOutput;
	if (!EvaluateOutput(Recipe, Context, Recipe.OutputNode, TEXT("Terrain"), TerrainOutput, &Result.Error)) return Result;
	if (TerrainOutput.Type != EEonformTerrainValueType::Terrain || !TerrainOutput.IsValid())
	{
		Result.Error = TEXT("Recipe output node produced no Terrain output.");
		return Result;
	}
	Result.Dataset = MoveTemp(TerrainOutput.TerrainDataset);
	Result.HeightScale = TerrainOutput.HeightScale;
	Result.bSuccess = true;
	return Result;
}
