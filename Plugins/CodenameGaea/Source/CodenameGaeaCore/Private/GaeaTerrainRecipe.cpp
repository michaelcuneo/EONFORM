#include "GaeaTerrainRecipe.h"

namespace GaeaTerrainNodeTypes
{
	const FName SourceDataset(TEXT("SourceDataset"));
	const FName HydraulicErosion(TEXT("HydraulicErosion"));
}

bool FGaeaTerrainNode::IsValid() const
{
	return Id.IsValid() && !Type.IsNone();
}

double FGaeaTerrainNode::GetNumber(FName Name, double DefaultValue) const
{
	if (const double* Value = NumericParameters.Find(Name)) return *Value;
	return DefaultValue;
}

int64 FGaeaTerrainNode::GetInteger(FName Name, int64 DefaultValue) const
{
	if (const int64* Value = IntegerParameters.Find(Name)) return *Value;
	return DefaultValue;
}

bool FGaeaTerrainNode::GetBool(FName Name, bool DefaultValue) const
{
	if (const bool* Value = BoolParameters.Find(Name)) return *Value;
	return DefaultValue;
}

FName FGaeaTerrainNode::GetName(FName Name, FName DefaultValue) const
{
	if (const FName* Value = NameParameters.Find(Name)) return *Value;
	return DefaultValue;
}

bool FGaeaTerrainConnection::IsValid() const
{
	return FromNode.IsValid() && ToNode.IsValid() && !FromOutput.IsNone() && !ToInput.IsNone();
}

const FGaeaTerrainNode* FGaeaTerrainRecipe::FindNode(const FGuid& NodeId) const
{
	return Nodes.FindByPredicate([&NodeId](const FGaeaTerrainNode& Node) { return Node.Id == NodeId; });
}

bool FGaeaTerrainRecipe::Validate(FString* OutError) const
{
	auto Fail = [OutError](const FString& Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (Version <= 0) return Fail(TEXT("Recipe version must be positive."));
	if (!OutputNode.IsValid()) return Fail(TEXT("Recipe has no valid output node."));
	if (Nodes.IsEmpty()) return Fail(TEXT("Recipe contains no nodes."));

	TSet<FGuid> NodeIds;
	for (const FGaeaTerrainNode& Node : Nodes)
	{
		if (!Node.IsValid()) return Fail(TEXT("Recipe contains an invalid node."));
		if (NodeIds.Contains(Node.Id)) return Fail(TEXT("Recipe contains duplicate node ids."));
		NodeIds.Add(Node.Id);
	}
	if (!NodeIds.Contains(OutputNode)) return Fail(TEXT("Recipe output node does not exist."));

	TSet<FString> InputKeys;
	for (const FGaeaTerrainConnection& Connection : Connections)
	{
		if (!Connection.IsValid()) return Fail(TEXT("Recipe contains an invalid connection."));
		if (!NodeIds.Contains(Connection.FromNode) || !NodeIds.Contains(Connection.ToNode)) return Fail(TEXT("Connection references a missing node."));
		const FString Key = Connection.ToNode.ToString(EGuidFormats::Digits) + TEXT(":") + Connection.ToInput.ToString();
		if (InputKeys.Contains(Key)) return Fail(TEXT("More than one connection targets the same node input."));
		InputKeys.Add(Key);
	}

	if (OutError) OutError->Reset();
	return true;
}

uint32 FGaeaTerrainRecipe::GetDeterministicHash() const
{
	uint32 Hash = GetTypeHash(Version);
	Hash = HashCombineFast(Hash, GetTypeHash(OutputNode));

	TArray<const FGaeaTerrainNode*> SortedNodes;
	for (const FGaeaTerrainNode& Node : Nodes) SortedNodes.Add(&Node);
	SortedNodes.Sort([](const FGaeaTerrainNode& A, const FGaeaTerrainNode& B) { return A.Id < B.Id; });

	for (const FGaeaTerrainNode* Node : SortedNodes)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Node->Id));
		Hash = HashCombineFast(Hash, GetTypeHash(Node->Type));

		auto HashMap = [&Hash](const auto& Map)
		{
			using PairType = typename TDecay<decltype(Map)>::Type::ElementType;
			TArray<const PairType*> Pairs;
			for (const auto& Pair : Map) Pairs.Add(&Pair);
			Pairs.Sort([](const PairType& A, const PairType& B) { return A.Key.LexicalLess(B.Key); });
			for (const PairType* Pair : Pairs)
			{
				Hash = HashCombineFast(Hash, GetTypeHash(Pair->Key));
				Hash = HashCombineFast(Hash, GetTypeHash(Pair->Value));
			}
		};

		HashMap(Node->NumericParameters);
		HashMap(Node->IntegerParameters);
		HashMap(Node->BoolParameters);
		HashMap(Node->NameParameters);
	}

	TArray<const FGaeaTerrainConnection*> SortedConnections;
	for (const FGaeaTerrainConnection& Connection : Connections) SortedConnections.Add(&Connection);
	SortedConnections.Sort([](const FGaeaTerrainConnection& A, const FGaeaTerrainConnection& B)
	{
		if (A.ToNode != B.ToNode) return A.ToNode < B.ToNode;
		if (A.ToInput != B.ToInput) return A.ToInput.LexicalLess(B.ToInput);
		if (A.FromNode != B.FromNode) return A.FromNode < B.FromNode;
		return A.FromOutput.LexicalLess(B.FromOutput);
	});
	for (const FGaeaTerrainConnection* Connection : SortedConnections)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->FromNode));
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->FromOutput));
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->ToNode));
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->ToInput));
	}
	return Hash;
}
