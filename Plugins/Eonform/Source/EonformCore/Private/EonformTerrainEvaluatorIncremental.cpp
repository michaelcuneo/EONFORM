#include "EonformTerrainEvaluator.h"

#include "EonformTerrainDomainScaling.h"
#include "HAL/PlatformTime.h"

namespace EonformIncrementalEvaluatorPrivate
{
	template<typename ValueType>
	void HashNamedMap(uint32& Hash, const TMap<FName, ValueType>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (const FName Key : Keys)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Key));
			Hash = HashCombineFast(Hash, GetTypeHash(Map.FindChecked(Key)));
		}
	}

	uint32 HashNodeParameters(const FEonformTerrainNode& Node, uint64 ContextRevision)
	{
		uint32 Hash = HashCombineFast(GetTypeHash(Node.Type), GetTypeHash(ContextRevision));
		HashNamedMap(Hash, Node.NumericParameters);
		HashNamedMap(Hash, Node.IntegerParameters);
		HashNamedMap(Hash, Node.BoolParameters);
		HashNamedMap(Hash, Node.NameParameters);
		return Hash;
	}

	bool ConnectionLess(const FEonformTerrainConnection& A, const FEonformTerrainConnection& B)
	{
		if (A.ToInput != B.ToInput) return A.ToInput.LexicalLess(B.ToInput);
		if (A.FromNode != B.FromNode) return A.FromNode < B.FromNode;
		return A.FromOutput.LexicalLess(B.FromOutput);
	}
}

FEonformTerrainEvaluationResult FEonformTerrainEvaluator::EvaluateIncremental(
	const FEonformTerrainRecipe& Recipe,
	const FEonformTerrainEvaluationContext& Context,
	FEonformTerrainEvaluationCache& PersistentCache)
{
	using namespace EonformIncrementalEvaluatorPrivate;

	const double StartSeconds = FPlatformTime::Seconds();
	FEonformTerrainEvaluationResult Result;
	Result.RecipeHash = Recipe.GetDeterministicHash();
	if (!Recipe.Validate(&Result.Error))
	{
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	if (Recipe.Nodes.IsEmpty())
	{
		Result = Evaluate(Recipe, Context);
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	FEonformTerrainNodeRegistry::RegisterBuiltIns();

	uint64 ContextRevision = Context.CacheContextRevision;
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.TargetResolution.X));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.TargetResolution.Y));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.ReferenceResolution.X));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.ReferenceResolution.Y));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.HeightScale));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.WorldWidthMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.WorldDepthMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.ElevationScaleMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.SeaLevelMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.HasRegion()));
	if (Context.HasRegion())
	{
		ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.Region.WorldMinCm.X));
		ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.Region.WorldMinCm.Y));
		ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.Region.WorldMaxCm.X));
		ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.Region.WorldMaxCm.Y));
		ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.Region.BorderSamples));
	}

	TSet<FGuid> Visiting;
	TMap<FGuid, uint32> CurrentSignatures;
	TSet<FGuid> ReachableNodes;

	TFunction<bool(const FGuid&)> EvaluateNode = [&](const FGuid& NodeId) -> bool
	{
		if (CurrentSignatures.Contains(NodeId)) return true;
		if (Visiting.Contains(NodeId))
		{
			Result.Error = TEXT("Terrain recipe contains a dependency cycle.");
			return false;
		}

		const FEonformTerrainNode* Node = Recipe.FindNode(NodeId);
		if (!Node)
		{
			Result.Error = TEXT("Evaluator could not resolve a node id.");
			return false;
		}

		Visiting.Add(NodeId);
		ReachableNodes.Add(NodeId);

		TArray<const FEonformTerrainConnection*> IncomingConnections;
		for (const FEonformTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode == NodeId)
			{
				IncomingConnections.Add(&Connection);
			}
		}
		IncomingConnections.Sort(ConnectionLess);

		for (const FEonformTerrainConnection* Connection : IncomingConnections)
		{
			if (!EvaluateNode(Connection->FromNode)) return false;
		}

		uint32 Signature = HashNodeParameters(*Node, ContextRevision);
		for (const FEonformTerrainConnection* Connection : IncomingConnections)
		{
			Signature = HashCombineFast(Signature, GetTypeHash(Connection->FromNode));
			Signature = HashCombineFast(Signature, GetTypeHash(Connection->FromOutput));
			Signature = HashCombineFast(Signature, GetTypeHash(Connection->ToInput));
			const uint32* UpstreamSignature = CurrentSignatures.Find(Connection->FromNode);
			if (!UpstreamSignature)
			{
				Result.Error = TEXT("Incremental evaluator lost an upstream node signature.");
				return false;
			}
			Signature = HashCombineFast(Signature, *UpstreamSignature);
		}
		CurrentSignatures.Add(NodeId, Signature);

		if (const FEonformTerrainEvaluationCacheEntry* Existing = PersistentCache.Nodes.Find(NodeId))
		{
			if (Existing->Signature == Signature && !Existing->Evaluation.Outputs.IsEmpty())
			{
				++Result.CachedNodeCount;
				Visiting.Remove(NodeId);
				return true;
			}
		}

		FEonformTerrainNodeInputs RawInputs;
		for (const FEonformTerrainConnection* Connection : IncomingConnections)
		{
			const FEonformTerrainEvaluationCacheEntry* SourceEntry = PersistentCache.Nodes.Find(Connection->FromNode);
			const FEonformTerrainValue* SourceOutput = SourceEntry
				? SourceEntry->Evaluation.FindOutput(Connection->FromOutput)
				: nullptr;
			if (!SourceOutput || !SourceOutput->IsValid())
			{
				Result.Error = FString::Printf(
					TEXT("Node connection references missing or invalid output '%s'."),
					*Connection->FromOutput.ToString());
				return false;
			}
			RawInputs.Add(Connection->ToInput, SourceOutput);
		}

		TMap<FName, FEonformTerrainValue> OwnedInputs;
		FEonformTerrainNodeInputs Inputs;
		if (!EonformTerrainDomainScaling::NormalizeInputs(RawInputs, Context, OwnedInputs, Inputs, &Result.Error))
		{
			return false;
		}

		const FEonformTerrainNodeEvaluator* Evaluator = FEonformTerrainNodeRegistry::Find(Node->Type);
		if (!Evaluator)
		{
			Result.Error = FString::Printf(TEXT("No evaluator registered for node type '%s'."), *Node->Type.ToString());
			return false;
		}

		FEonformTerrainNodeEvaluation NodeResult;
		if (!(*Evaluator)(*Node, Inputs, Context, NodeResult, Result.Error)) return false;
		if (NodeResult.Outputs.IsEmpty())
		{
			Result.Error = FString::Printf(TEXT("Node '%s' produced no outputs."), *Node->Type.ToString());
			return false;
		}
		if (!EonformTerrainDomainScaling::NormalizeOutputs(NodeResult, Context, &Result.Error)) return false;

		FEonformTerrainEvaluationCacheEntry NewEntry;
		NewEntry.Signature = Signature;
		NewEntry.Evaluation = MoveTemp(NodeResult);
		PersistentCache.Nodes.Add(NodeId, MoveTemp(NewEntry));
		++Result.EvaluatedNodeCount;
		Visiting.Remove(NodeId);
		return true;
	};

	if (!EvaluateNode(Recipe.OutputNode))
	{
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	const FEonformTerrainEvaluationCacheEntry* OutputEntry = PersistentCache.Nodes.Find(Recipe.OutputNode);
	const FEonformTerrainValue* TerrainOutput = OutputEntry
		? OutputEntry->Evaluation.FindOutput(TEXT("Terrain"))
		: nullptr;
	if (!TerrainOutput || TerrainOutput->Type != EEonformTerrainValueType::Terrain || !TerrainOutput->IsValid())
	{
		Result.Error = TEXT("Recipe output node produced no Terrain output.");
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	TArray<FGuid> CachedIds;
	PersistentCache.Nodes.GetKeys(CachedIds);
	for (const FGuid& CachedId : CachedIds)
	{
		if (!ReachableNodes.Contains(CachedId))
		{
			PersistentCache.Nodes.Remove(CachedId);
		}
	}

	Result.Dataset = TerrainOutput->TerrainDataset;
	Result.HeightScale = TerrainOutput->HeightScale;
	Result.bSuccess = true;
	Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}
