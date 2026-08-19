#include "GaeaTerrainEvaluator.h"

#include "GaeaGridDomain.h"
#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainContext.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNoise.h"
#include "GaeaTerrainShaping.h"

namespace
{
	FCriticalSection RegistryMutex;
	TMap<FName, FGaeaTerrainNodeEvaluator> Registry;

	bool EvaluateSourceDataset(
		const FGaeaTerrainNode&,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		if (Context.SourceDataset.IsEmpty())
		{
			Error = TEXT("SourceDataset node received an empty source dataset.");
			return false;
		}
		Out.Dataset = Context.SourceDataset;
		Out.HeightScale = FMath::Max(Context.HeightScale, 1.0f);
		return true;
	}

	bool EvaluateProceduralTerrain(
		const FGaeaTerrainNode& Node,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>&,
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
			Error = TEXT("ProceduralTerrain produced an invalid grid domain.");
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

		Out.Dataset.Reset();
		if (!Out.Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("ProceduralTerrain could not publish its Height field.");
			return false;
		}
		Out.HeightScale = HeightScale;
		return true;
	}

	bool EvaluateTerrainShapeNode(
		const FGaeaTerrainNode& Node,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainNodeEvaluation* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		if (!InputPtr || !*InputPtr)
		{
			Error = TEXT("TerrainShape requires a Terrain input.");
			return false;
		}

		const FGaeaTerrainNodeEvaluation& Input = **InputPtr;
		const FGaeaScalarField* Height = Input.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
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

		Out.Dataset = Input.Dataset;
		Out.HeightScale = Input.HeightScale;
		return FGaeaTerrainShaping::Apply(*Height, Settings, Out.Dataset, &Error);
	}

	bool EvaluateTerrainContextNode(
		const FGaeaTerrainNode&,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainNodeEvaluation* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		if (!InputPtr || !*InputPtr)
		{
			Error = TEXT("TerrainContext requires a Terrain input.");
			return false;
		}

		const FGaeaTerrainNodeEvaluation& Input = **InputPtr;
		const FGaeaScalarField* Height = Input.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("TerrainContext input dataset has no Height field.");
			return false;
		}

		Out.Dataset = Input.Dataset;
		Out.HeightScale = Input.HeightScale;
		return FGaeaTerrainContext::Analyze(*Height, FMath::Max(Input.HeightScale, 1.0f), Out.Dataset, &Error);
	}

	bool EvaluateProcessMasksNode(
		const FGaeaTerrainNode& Node,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainNodeEvaluation* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		if (!InputPtr || !*InputPtr)
		{
			Error = TEXT("ProcessMasks requires a Terrain input.");
			return false;
		}

		const FGaeaTerrainNodeEvaluation& Input = **InputPtr;
		const FGaeaScalarField* Height = Input.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
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

		Out.Dataset = Input.Dataset;
		Out.HeightScale = Input.HeightScale;
		return FGaeaTerrainContext::BuildProcessMasks(*Height, Settings, Out.Dataset, &Error);
	}

	bool EvaluateHydraulicErosionNode(
		const FGaeaTerrainNode& Node,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainNodeEvaluation* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		if (!InputPtr || !*InputPtr)
		{
			Error = TEXT("HydraulicErosion requires a Terrain input.");
			return false;
		}
		const FGaeaTerrainNodeEvaluation& Input = **InputPtr;
		const FGaeaTerrainDataset& InputDataset = Input.Dataset;
		const FGaeaScalarField* Height = InputDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height)
		{
			Error = TEXT("HydraulicErosion input dataset has no Height field.");
			return false;
		}

		FGaeaHydraulicErosionSettings Settings;
		Settings.Iterations = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), Settings.Iterations)), 1, 4096);
		Settings.Rainfall = static_cast<float>(Node.GetNumber(TEXT("Rainfall"), Settings.Rainfall));
		Settings.FlowRate = static_cast<float>(Node.GetNumber(TEXT("FlowRate"), Settings.FlowRate));
		Settings.SedimentCapacity = static_cast<float>(Node.GetNumber(TEXT("SedimentCapacity"), Settings.SedimentCapacity));
		Settings.ErosionRate = static_cast<float>(Node.GetNumber(TEXT("ErosionRate"), Settings.ErosionRate));
		Settings.DepositionRate = static_cast<float>(Node.GetNumber(TEXT("DepositionRate"), Settings.DepositionRate));
		Settings.Evaporation = static_cast<float>(Node.GetNumber(TEXT("Evaporation"), Settings.Evaporation));
		Settings.MinimumSlope = static_cast<float>(Node.GetNumber(TEXT("MinimumSlope"), Settings.MinimumSlope));

		FGaeaHydraulicErosionResult Result;
		if (!FGaeaHydraulicErosion::Evaluate(
			*Height,
			FMath::Max(Input.HeightScale, 1.0f),
			Settings,
			Result,
			InputDataset.FindScalarField(GaeaTerrainFieldNames::Rainfall),
			InputDataset.FindScalarField(GaeaTerrainFieldNames::HydraulicErosion),
			InputDataset.FindScalarField(GaeaTerrainFieldNames::Deposition),
			InputDataset.FindScalarField(GaeaTerrainFieldNames::Evaporation),
			InputDataset.FindScalarField(GaeaTerrainFieldNames::RockHardness),
			InputDataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth)))
		{
			Error = TEXT("Hydraulic erosion evaluation failed.");
			return false;
		}

		Out.Dataset = InputDataset;
		Out.Dataset.SetScalarField(MoveTemp(Result.Height));
		Out.Dataset.SetScalarField(MoveTemp(Result.Wear));
		Out.Dataset.SetScalarField(MoveTemp(Result.Deposits));
		Out.Dataset.SetScalarField(MoveTemp(Result.Flow));
		Out.HeightScale = Input.HeightScale;
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
	if (!Registry.Contains(GaeaTerrainNodeTypes::ProceduralTerrain)) Registry.Add(GaeaTerrainNodeTypes::ProceduralTerrain, EvaluateProceduralTerrain);
	if (!Registry.Contains(GaeaTerrainNodeTypes::TerrainShape)) Registry.Add(GaeaTerrainNodeTypes::TerrainShape, EvaluateTerrainShapeNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::TerrainContext)) Registry.Add(GaeaTerrainNodeTypes::TerrainContext, EvaluateTerrainContextNode);
	if (!Registry.Contains(GaeaTerrainNodeTypes::ProcessMasks)) Registry.Add(GaeaTerrainNodeTypes::ProcessMasks, EvaluateProcessMasksNode);
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
		TMap<FName, const FGaeaTerrainNodeEvaluation*> Inputs;
		for (const FGaeaTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode != NodeId) continue;
			if (!EvaluateNode(Connection.FromNode)) return false;
			Inputs.Add(Connection.ToInput, Cache.Find(Connection.FromNode));
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
		Cache.Add(NodeId, MoveTemp(NodeResult));
		Visiting.Remove(NodeId);
		return true;
	};

	if (!EvaluateNode(Recipe.OutputNode)) return Result;
	const FGaeaTerrainNodeEvaluation* Output = Cache.Find(Recipe.OutputNode);
	if (!Output)
	{
		Result.Error = TEXT("Recipe output node produced no result.");
		return Result;
	}
	Result.Dataset = Output->Dataset;
	Result.HeightScale = Output->HeightScale;
	Result.bSuccess = true;
	return Result;
}
