#include "GaeaTerrainEvaluator.h"

#include "GaeaHydraulicErosion.h"
#include "GaeaTerrainFieldNames.h"

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
		return true;
	}

	bool EvaluateHydraulicErosionNode(
		const FGaeaTerrainNode& Node,
		const TMap<FName, const FGaeaTerrainNodeEvaluation*>& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainNodeEvaluation* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		if (!InputPtr || !*InputPtr)
		{
			Error = TEXT("HydraulicErosion requires a Terrain input.");
			return false;
		}
		const FGaeaTerrainDataset& InputDataset = (*InputPtr)->Dataset;
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
			Context.HeightScale,
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
	Result.bSuccess = true;
	return Result;
}
