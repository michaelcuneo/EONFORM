#include "GaeaTerrainEvaluator.h"

#include "GaeaGridDomain.h"
#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainContext.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainGeology.h"
#include "GaeaTerrainNoise.h"
#include "GaeaTerrainShaping.h"

namespace
{
	FCriticalSection RegistryMutex;
	TMap<FName, FGaeaTerrainNodeEvaluator> Registry;

	const FGaeaTerrainValue* FindInput(const FGaeaTerrainNodeInputs& Inputs, FName Name)
	{
		const FGaeaTerrainValue* const* Value = Inputs.Find(Name);
		return Value ? *Value : nullptr;
	}

	const FGaeaTerrainValue* RequireTerrainInput(
		const FGaeaTerrainNodeInputs& Inputs,
		FName Name,
		const TCHAR* NodeName,
		FString& Error)
	{
		const FGaeaTerrainValue* Value = FindInput(Inputs, Name);
		if (!Value || Value->Type != EGaeaTerrainValueType::Terrain || !Value->IsValid())
		{
			Error = FString::Printf(TEXT("%s requires a terrain input '%s'."), NodeName, *Name.ToString());
			return nullptr;
		}
		return Value;
	}

	const FGaeaScalarField* OptionalScalarInput(
		const FGaeaTerrainNodeInputs& Inputs,
		FName Name,
		const FGaeaGridDomain& Domain,
		FString& Error)
	{
		const FGaeaTerrainValue* Value = FindInput(Inputs, Name);
		if (!Value) return nullptr;
		if (Value->Type != EGaeaTerrainValueType::ScalarField || !Value->ScalarField.IsValid())
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
		FGaeaTerrainNodeEvaluation& Out,
		FGaeaTerrainDataset&& Dataset,
		float HeightScale,
		FString& Error)
	{
		FGaeaTerrainValue Value = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid())
		{
			Error = TEXT("Terrain node produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Terrain"), MoveTemp(Value));
		return true;
	}

	bool BuildFlatTerrainResult(FGaeaTerrainEvaluationResult& Result)
	{
		constexpr int32 Resolution = 257;
		constexpr double WorldSize = 100000.0;
		constexpr float HeightScale = 8000.0f;
		const double HalfWorldSize = WorldSize * 0.5;

		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Result.Error = TEXT("Could not create the default flat terrain domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField Height;
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
		const FGaeaTerrainNode&,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		if (Context.SourceDataset.IsEmpty())
		{
			Error = TEXT("SourceDataset node received an empty source dataset.");
			return false;
		}
		FGaeaTerrainDataset Dataset = Context.SourceDataset;
		return PublishTerrain(Out, MoveTemp(Dataset), FMath::Max(Context.HeightScale, 1.0f), Error);
	}

	bool EvaluatePerlinNoise(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const int32 Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), 1337), MIN_int32, MAX_int32));

		FGaeaFractalNoiseSettings Settings;
		Settings.Frequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Frequency"), Settings.Frequency)), 0.000001f, 1.0f);
		Settings.Octaves = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), Settings.Octaves)), 1, 16);
		Settings.Persistence = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Persistence"), Settings.Persistence)), 0.0f, 1.0f);
		Settings.Lacunarity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Lacunarity"), Settings.Lacunarity)), 1.0f, 8.0f);

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("PerlinNoise produced an invalid grid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor);
		const FVector2D SeedOffset = FGaeaTerrainNoise::MakeSeedOffset(Seed);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				Height.AtInterior(X, Y) = FGaeaTerrainNoise::SampleFractal(
					FVector2D(static_cast<float>(World.X), static_cast<float>(World.Y)),
					SeedOffset,
					Settings);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("PerlinNoise could not publish its Height field.");
			return false;
		}
		return PublishTerrain(Out, MoveTemp(Dataset), HeightScale, Error);
	}

	bool EvaluateTerrainShapeNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("TerrainShape"), Error);
		if (!Input) return false;

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("TerrainShape input dataset has no Height field.");
			return false;
		}

		FGaeaTerrainShapeSettings Settings;
		Settings.Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), Settings.Seed), MIN_int32, MAX_int32));
		Settings.MacroStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MacroStrength"), Settings.MacroStrength)), 0.0f, 2.0f);
		Settings.MountainThreshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MountainThreshold"), Settings.MountainThreshold)), -1.0f, 1.0f);
		Settings.WarpStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpStrength"), Settings.WarpStrength)), 0.0f, 25000.0f);
		Settings.RidgeStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RidgeStrength"), Settings.RidgeStrength)), 0.0f, 2.0f);
		Settings.FoothillStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("FoothillStrength"), Settings.FoothillStrength)), 0.0f, 1.0f);
		Settings.ValleyDepth = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ValleyDepth"), Settings.ValleyDepth)), 0.0f, 1.0f);
		Settings.PlainsStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("PlainsStrength"), Settings.PlainsStrength)), 0.0f, 1.0f);

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainShaping::Apply(*Height, Settings, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateTerrainContextNode(
		const FGaeaTerrainNode&,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("TerrainContext"), Error);
		if (!Input) return false;
		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("TerrainContext input dataset has no Height field.");
			return false;
		}

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainContext::Analyze(
			*Height,
			FMath::Max(Input->HeightScale, 1.0f),
			Context.PhysicalMetrics,
			Dataset,
			&Error))
		{
			return false;
		}
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateGeologyNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("Geology"), Error);
		if (!Input) return false;
		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("Geology input dataset has no Height field.");
			return false;
		}

		FGaeaTerrainGeologySettings Settings;
		const int32 Seed = static_cast<int32>(FMath::Clamp<int64>(Node.GetInteger(TEXT("Seed"), 1337), MIN_int32, MAX_int32));
		Settings.Frequency = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Frequency"), Settings.Frequency)), 0.000001f, 1.0f);
		Settings.Octaves = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), Settings.Octaves)), 1, 16);
		Settings.Contrast = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Contrast"), Settings.Contrast)), 0.1f, 4.0f);
		Settings.MountainHardnessBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MountainHardnessBias"), Settings.MountainHardnessBias)), 0.0f, 1.0f);
		Settings.PlainsSoftnessBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("PlainsSoftnessBias"), Settings.PlainsSoftnessBias)), 0.0f, 1.0f);
		Settings.SoilFormationStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SoilFormationStrength"), Settings.SoilFormationStrength)), 0.0f, 2.0f);

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainGeology::Build(*Height, Seed, Settings, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateProcessMasksNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("ProcessMasks"), Error);
		if (!Input) return false;
		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("ProcessMasks input dataset has no Height field.");
			return false;
		}

		FGaeaTerrainProcessMaskSettings Settings;
		Settings.ThermalTalusAngleDegrees = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ThermalTalusAngle"), Settings.ThermalTalusAngleDegrees)), 0.0f, 90.0f);
		Settings.ThermalRegionality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ThermalRegionality"), Settings.ThermalRegionality)), 0.0f, 1.0f);
		Settings.HydraulicRegionality = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HydraulicRegionality"), Settings.HydraulicRegionality)), 0.0f, 1.0f);
		Settings.RainfallHighlandBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RainfallHighlandBias"), Settings.RainfallHighlandBias)), 0.0f, 1.0f);
		Settings.EvaporationLowlandBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("EvaporationLowlandBias"), Settings.EvaporationLowlandBias)), 0.0f, 1.0f);

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainContext::BuildProcessMasks(*Height, Settings, Dataset, &Error)) return false;
		return PublishTerrain(Out, MoveTemp(Dataset), Input->HeightScale, Error);
	}

	bool EvaluateSlopeNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("Slope"), Error);
		if (!Input) return false;

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Slope input terrain has no valid Height field.");
			return false;
		}

		const float MinSlope = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Min"), 0.0)), 0.0f, 90.0f);
		const float MaxSlope = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Max"), 45.0)), MinSlope, 90.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 5.0)), 0.0f, 45.0f);
		const FIntPoint Dimensions = Height->Domain.Dimensions;
		const FVector2d CellSizeMeters = Context.PhysicalMetrics.ResolveSampleSpacingMeters(
			Dimensions,
			Height->Domain.GetCellSize());
		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		if (CellSizeMeters.X <= UE_DOUBLE_SMALL_NUMBER || CellSizeMeters.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Slope input terrain has an invalid physical grid spacing.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = TEXT("SlopeMask");
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Mask;
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
				if (SlopeDegrees >= MinSlope && SlopeDegrees <= MaxSlope)
				{
					Weight = 1.0f;
				}
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
		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Mask)));
		return true;
	}

	bool EvaluateHydraulicErosionNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* Input = RequireTerrainInput(Inputs, TEXT("Terrain"), TEXT("Erosion"), Error);
		if (!Input) return false;

		FGaeaTerrainDataset PreparedDataset = Input->TerrainDataset;
		FGaeaTerrainDerivedDataSettings DerivedSettings;
		if (!FGaeaTerrainDerivedData::EnsureHydraulicInputs(
			PreparedDataset,
			FMath::Max(Input->HeightScale, 1.0f),
			Context.PhysicalMetrics,
			DerivedSettings,
			&Error))
		{
			return false;
		}

		const FGaeaScalarField* Height = PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("Erosion input dataset has no Height field.");
			return false;
		}

		const bool bHasAreaInput = FindInput(Inputs, TEXT("Mask")) != nullptr;
		const FGaeaScalarField* AreaMask = OptionalScalarInput(Inputs, TEXT("Mask"), Height->Domain, Error);
		if (bHasAreaInput && !AreaMask) return false;
		const bool bHasSedimentInput = FindInput(Inputs, TEXT("Sediment")) != nullptr;
		const FGaeaScalarField* SedimentInput = OptionalScalarInput(Inputs, TEXT("Sediment"), Height->Domain, Error);
		if (bHasSedimentInput && !SedimentInput) return false;

		FGaeaHydraulicErosionSettings Settings;
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
		const double DomainRepresentativeCentimeters = FMath::Max(
			FMath::Min(FMath::Abs(DomainCellSize.X), FMath::Abs(DomainCellSize.Y)),
			UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSampleSpacingMeters = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(
			Height->Domain.Dimensions,
			DomainCellSize);
		const double PhysicalElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		Settings.PhysicalSampleSpacingMeters = PhysicalSampleSpacingMeters;
		Settings.PhysicalElevationScaleMeters = PhysicalElevationScaleMeters;

		// Preserve the existing stable hydraulic solver while changing the height/cell
		// ratio it sees. The normalized output remains identical in representation,
		// but slopes now correspond to the graph's physical world dimensions.
		const float SolverHeightScale = static_cast<float>(FMath::Max(
			PhysicalElevationScaleMeters / FMath::Max(PhysicalSampleSpacingMeters, UE_DOUBLE_SMALL_NUMBER)
				* DomainRepresentativeCentimeters,
			1.0));

		FGaeaHydraulicErosionResult Result;
		if (!FGaeaHydraulicErosion::Evaluate(
			*Height,
			SolverHeightScale,
			Settings,
			Result,
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Rainfall),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::HydraulicErosion),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Deposition),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::Evaporation),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			PreparedDataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth),
			AreaMask,
			SedimentInput))
		{
			Error = TEXT("Erosion evaluation failed.");
			return false;
		}

		FGaeaScalarField WearOutput = Result.Wear;
		FGaeaScalarField DepositsOutput = Result.Deposits;
		FGaeaScalarField FlowOutput = Result.Flow;

		PreparedDataset.SetScalarField(MoveTemp(Result.Height));
		PreparedDataset.SetScalarField(MoveTemp(Result.Wear));
		PreparedDataset.SetScalarField(MoveTemp(Result.Deposits));
		PreparedDataset.SetScalarField(MoveTemp(Result.Flow));
		if (!PublishTerrain(Out, MoveTemp(PreparedDataset), Input->HeightScale, Error)) return false;

		Out.Outputs.Add(TEXT("Wear"), FGaeaTerrainValue::MakeScalarField(MoveTemp(WearOutput)));
		Out.Outputs.Add(TEXT("Deposits"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepositsOutput)));
		Out.Outputs.Add(TEXT("Flow"), FGaeaTerrainValue::MakeScalarField(MoveTemp(FlowOutput)));
		return true;
	}
}

void FGaeaTerrainNodeRegistry::Register(FName NodeType, FGaeaTerrainNodeEvaluator Evaluator)
{
	if (NodeType.IsNone() || !Evaluator) return;
	FScopeLock Lock(&RegistryMutex);
	Registry.Add(NodeType, MoveTemp(Evaluator));
}

bool FGaeaTerrainNodeRegistry::IsRegistered(FName NodeType)
{
	FScopeLock Lock(&RegistryMutex);
	return Registry.Contains(NodeType);
}

void FGaeaTerrainNodeRegistry::RegisterBuiltIns()
{
	FScopeLock Lock(&RegistryMutex);
	if (!Registry.Contains(GaeaTerrainNodeTypes::SourceDataset)) Registry.Add(GaeaTerrainNodeTypes::SourceDataset, EvaluateSourceDataset);
	if (!Registry.Contains(GaeaTerrainNodeTypes::PerlinNoise)) Registry.Add(GaeaTerrainNodeTypes::PerlinNoise, EvaluatePerlinNoise);
	if (!Registry.Contains(GaeaTerrainNodeTypes::TerrainShape)) Registry.Add(GaeaTerrainNodeTypes::TerrainShape, EvaluateTerrainShapeNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::TerrainContext)) Registry.Add(GaeaTerrainNodeTypes::TerrainContext, EvaluateTerrainContextNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::Geology)) Registry.Add(GaeaTerrainNodeTypes::Geology, EvaluateGeologyNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::ProcessMasks)) Registry.Add(GaeaTerrainNodeTypes::ProcessMasks, EvaluateProcessMasksNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::Slope)) Registry.Add(GaeaTerrainNodeTypes::Slope, EvaluateSlopeNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::HydraulicErosion)) Registry.Add(GaeaTerrainNodeTypes::HydraulicErosion, EvaluateHydraulicErosionNode);
}

void FGaeaTerrainNodeRegistry::Reset()
{
	FScopeLock Lock(&RegistryMutex);
	Registry.Reset();
}

const FGaeaTerrainNodeEvaluator* FGaeaTerrainNodeRegistry::Find(FName NodeType)
{
	return Registry.Find(NodeType);
}

FGaeaTerrainEvaluationResult FGaeaTerrainEvaluator::Evaluate(
	const FGaeaTerrainRecipe& Recipe,
	const FGaeaTerrainEvaluationContext& Context)
{
	FGaeaTerrainEvaluationResult Result;
	Result.RecipeHash = Recipe.GetDeterministicHash();
	if (!Recipe.Validate(&Result.Error)) return Result;

	if (Recipe.Nodes.IsEmpty())
	{
		BuildFlatTerrainResult(Result);
		return Result;
	}

	FGaeaTerrainNodeRegistry::RegisterBuiltIns();

	TMap<FGuid, FGaeaTerrainNodeEvaluation> Cache;
	TSet<FGuid> Visiting;

	TFunction<bool(const FGuid&)> EvaluateNode = [&](const FGuid& NodeId) -> bool
	{
		if (Cache.Contains(NodeId)) return true;
		if (Visiting.Contains(NodeId))
		{
			Result.Error = TEXT("Terrain recipe contains a dependency cycle.");
			return false;
		}

		const FGaeaTerrainNode* Node = Recipe.FindNode(NodeId);
		if (!Node)
		{
			Result.Error = TEXT("Evaluator could not resolve a node id.");
			return false;
		}

		Visiting.Add(NodeId);
		FGaeaTerrainNodeInputs Inputs;
		for (const FGaeaTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode != NodeId) continue;
			if (!EvaluateNode(Connection.FromNode)) return false;

			const FGaeaTerrainNodeEvaluation* SourceEvaluation = Cache.Find(Connection.FromNode);
			const FGaeaTerrainValue* SourceOutput = SourceEvaluation ? SourceEvaluation->FindOutput(Connection.FromOutput) : nullptr;
			if (!SourceOutput || !SourceOutput->IsValid())
			{
				Result.Error = FString::Printf(
					TEXT("Node connection references missing or invalid output '%s'."),
					*Connection.FromOutput.ToString());
				return false;
			}
			Inputs.Add(Connection.ToInput, SourceOutput);
		}

		FGaeaTerrainNodeEvaluator Evaluator;
		{
			FScopeLock Lock(&RegistryMutex);
			const FGaeaTerrainNodeEvaluator* Found = FGaeaTerrainNodeRegistry::Find(Node->Type);
			if (!Found)
			{
				Result.Error = FString::Printf(TEXT("No evaluator registered for node type '%s'."), *Node->Type.ToString());
				return false;
			}
			Evaluator = *Found;
		}

		FGaeaTerrainNodeEvaluation NodeResult;
		if (!Evaluator(*Node, Inputs, Context, NodeResult, Result.Error)) return false;
		if (NodeResult.Outputs.IsEmpty())
		{
			Result.Error = FString::Printf(TEXT("Node '%s' produced no outputs."), *Node->Type.ToString());
			return false;
		}
		Cache.Add(NodeId, MoveTemp(NodeResult));
		Visiting.Remove(NodeId);
		return true;
	};

	if (!EvaluateNode(Recipe.OutputNode)) return Result;
	const FGaeaTerrainNodeEvaluation* Output = Cache.Find(Recipe.OutputNode);
	const FGaeaTerrainValue* TerrainOutput = Output ? Output->FindOutput(TEXT("Terrain")) : nullptr;
	if (!TerrainOutput || TerrainOutput->Type != EGaeaTerrainValueType::Terrain || !TerrainOutput->IsValid())
	{
		Result.Error = TEXT("Recipe output node produced no Terrain output.");
		return Result;
	}
	Result.Dataset = TerrainOutput->TerrainDataset;
	Result.HeightScale = TerrainOutput->HeightScale;
	Result.bSuccess = true;
	return Result;
}
