#include "GaeaTerrainEvaluator.h"

#include "HAL/PlatformTime.h"

namespace
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

	uint32 HashNodeParameters(const FGaeaTerrainNode& Node, uint64 ContextRevision)
	{
		uint32 Hash = HashCombineFast(GetTypeHash(Node.Type), GetTypeHash(ContextRevision));
		HashNamedMap(Hash, Node.NumericParameters);
		HashNamedMap(Hash, Node.IntegerParameters);
		HashNamedMap(Hash, Node.BoolParameters);
		HashNamedMap(Hash, Node.NameParameters);
		return Hash;
	}

	bool ConnectionLess(const FGaeaTerrainConnection& A, const FGaeaTerrainConnection& B)
	{
		if (A.ToInput != B.ToInput) return A.ToInput.LexicalLess(B.ToInput);
		if (A.FromNode != B.FromNode) return A.FromNode < B.FromNode;
		return A.FromOutput.LexicalLess(B.FromOutput);
	}
}

FGaeaTerrainEvaluationResult FGaeaTerrainEvaluator::EvaluateIncremental(
	const FGaeaTerrainRecipe& Recipe,
	const FGaeaTerrainEvaluationContext& Context,
	FGaeaTerrainEvaluationCache& PersistentCache)
{
	const double StartSeconds = FPlatformTime::Seconds();
	FGaeaTerrainEvaluationResult Result;
	Result.RecipeHash = Recipe.GetDeterministicHash();
	if (!Recipe.Validate(&Result.Error))
	{
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	if (Recipe.Nodes.IsEmpty())
	{
		// Preserve the existing flat-terrain contract without duplicating its
		// construction details in the incremental evaluator.
		Result = Evaluate(Recipe, Context);
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	FGaeaTerrainNodeRegistry::RegisterBuiltIns();

	// Physical metrics are part of the node result contract even when the caller
	// did not provide an explicit context revision.
	uint64 ContextRevision = Context.CacheContextRevision;
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.HeightScale));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.WorldWidthMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.WorldDepthMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.ElevationScaleMeters));
	ContextRevision = HashCombineFast(GetTypeHash(ContextRevision), GetTypeHash(Context.PhysicalMetrics.SeaLevelMeters));

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

		const FGaeaTerrainNode* Node = Recipe.FindNode(NodeId);
		if (!Node)
		{
			Result.Error = TEXT("Evaluator could not resolve a node id.");
			return false;
		}

		Visiting.Add(NodeId);
		ReachableNodes.Add(NodeId);

		TArray<const FGaeaTerrainConnection*> IncomingConnections;
		for (const FGaeaTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode == NodeId)
			{
				IncomingConnections.Add(&Connection);
			}
		}
		IncomingConnections.Sort(ConnectionLess);

		// Resolve every dependency before taking pointers into the persistent map.
		// TMap growth during recursive evaluation must not invalidate input pointers.
		for (const FGaeaTerrainConnection* Connection : IncomingConnections)
		{
			if (!EvaluateNode(Connection->FromNode)) return false;
		}

		uint32 Signature = HashNodeParameters(*Node, ContextRevision);
		for (const FGaeaTerrainConnection* Connection : IncomingConnections)
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

		if (const FGaeaTerrainEvaluationCacheEntry* Existing = PersistentCache.Nodes.Find(NodeId))
		{
			if (Existing->Signature == Signature && !Existing->Evaluation.Outputs.IsEmpty())
			{
				++Result.CachedNodeCount;
				Visiting.Remove(NodeId);
				return true;
			}
		}

		FGaeaTerrainNodeInputs Inputs;
		for (const FGaeaTerrainConnection* Connection : IncomingConnections)
		{
			const FGaeaTerrainEvaluationCacheEntry* SourceEntry = PersistentCache.Nodes.Find(Connection->FromNode);
			const FGaeaTerrainValue* SourceOutput = SourceEntry
				? SourceEntry->Evaluation.FindOutput(Connection->FromOutput)
				: nullptr;
			if (!SourceOutput || !SourceOutput->IsValid())
			{
				Result.Error = FString::Printf(
					TEXT("Node connection references missing or invalid output '%s'."),
					*Connection->FromOutput.ToString());
				return false;
			}
			Inputs.Add(Connection->ToInput, SourceOutput);
		}

		const FGaeaTerrainNodeEvaluator* Evaluator = FGaeaTerrainNodeRegistry::Find(Node->Type);
		if (!Evaluator)
		{
			Result.Error = FString::Printf(TEXT("No evaluator registered for node type '%s'."), *Node->Type.ToString());
			return false;
		}

		FGaeaTerrainNodeEvaluation NodeResult;
		if (!(*Evaluator)(*Node, Inputs, Context, NodeResult, Result.Error)) return false;
		if (NodeResult.Outputs.IsEmpty())
		{
			Result.Error = FString::Printf(TEXT("Node '%s' produced no outputs."), *Node->Type.ToString());
			return false;
		}

		FGaeaTerrainEvaluationCacheEntry NewEntry;
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

	const FGaeaTerrainEvaluationCacheEntry* OutputEntry = PersistentCache.Nodes.Find(Recipe.OutputNode);
	const FGaeaTerrainValue* TerrainOutput = OutputEntry
		? OutputEntry->Evaluation.FindOutput(TEXT("Terrain"))
		: nullptr;
	if (!TerrainOutput || TerrainOutput->Type != EGaeaTerrainValueType::Terrain || !TerrainOutput->IsValid())
	{
		Result.Error = TEXT("Recipe output node produced no Terrain output.");
		Result.EvaluationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		return Result;
	}

	// Drop orphaned cache entries after graph rewiring/deletion so interactive
	// editing does not retain large terrain datasets indefinitely.
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
