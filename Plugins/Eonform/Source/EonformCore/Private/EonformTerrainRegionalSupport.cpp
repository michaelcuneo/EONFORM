#include "EonformTerrainRegionalSupport.h"

FString FEonformTerrainRegionalSupportReport::Describe() const
{
	if (bSupported) return TEXT("All reachable terrain nodes support independent regional evaluation.");
	if (Reasons.IsEmpty()) return TEXT("The graph contains nodes that do not yet support independent regional evaluation.");
	return FString::Join(Reasons, TEXT("; "));
}

FEonformTerrainRegionalSupportReport FEonformTerrainRegionalSupport::Analyze(const FEonformTerrainRecipe& Recipe)
{
	FEonformTerrainRegionalSupportReport Report;

	TSet<FGuid> Reachable;
	TFunction<void(const FGuid&)> Visit = [&](const FGuid& NodeId)
	{
		if (!NodeId.IsValid() || Reachable.Contains(NodeId)) return;
		Reachable.Add(NodeId);
		for (const FEonformTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode == NodeId) Visit(Connection.FromNode);
		}
	};
	Visit(Recipe.OutputNode);

	for (const FEonformTerrainNode& Node : Recipe.Nodes)
	{
		if (!Reachable.Contains(Node.Id)) continue;

		bool bNodeSupported = false;
		FString Reason;

		if (Node.Type == EonformTerrainNodeTypes::PerlinNoise || Node.Type == EonformTerrainNodeTypes::Voronoi)
		{
			bNodeSupported = true;
		}
		else if (Node.Type == EonformTerrainNodeTypes::Constant)
		{
			const FName Output = Node.GetName(TEXT("Output"), TEXT("Height"));
			bNodeSupported = Output != TEXT("Noise");
			if (!bNodeSupported) Reason = TEXT("Constant Noise still hashes local sample coordinates");
		}
		else if (Node.Type == EonformTerrainNodeTypes::Combine
			|| Node.Type == EonformTerrainNodeTypes::Clamp
			|| Node.Type == EonformTerrainNodeTypes::Invert
			|| Node.Type == EonformTerrainNodeTypes::Threshold
			|| Node.Type == EonformTerrainNodeTypes::Terrace)
		{
			// These operations are pointwise/value-local once their inputs share the
			// requested regional domain. Terrace randomness depends on value/seed,
			// not absolute sample index.
			bNodeSupported = true;
		}
		else if (Node.Type == EonformTerrainNodeTypes::Ridge)
		{
			Reason = TEXT("Ridge still subtracts a whole-field minimum and requires a global-summary contract");
		}
		else if (Node.Type == EonformTerrainNodeTypes::Mountain)
		{
			Reason = TEXT("Mountain depends on Ridge/global range behaviour and is not region-safe yet");
		}
		else if (Node.Type == EonformTerrainNodeTypes::AutoLevel || Node.Type == EonformTerrainNodeTypes::Equalize)
		{
			Reason = TEXT("global normalization requires cached world statistics");
		}
		else if (Node.Type == EonformTerrainNodeTypes::HydraulicErosion
			|| Node.Type == EonformTerrainNodeTypes::Rivers
			|| Node.Type == EonformTerrainNodeTypes::FlowMap
			|| Node.Type == EonformTerrainNodeTypes::FlowMapClassic)
		{
			Reason = TEXT("hydrology requires macro/global drainage state before independent regional refinement");
		}
		else
		{
			Reason = FString::Printf(TEXT("node type '%s' has not yet been proven region-equivalent"), *Node.Type.ToString());
		}

		if (!bNodeSupported)
		{
			Report.UnsupportedNodes.Add(Node.Id);
			Report.Reasons.Add(FString::Printf(TEXT("%s: %s"), *Node.Type.ToString(), *Reason));
		}
	}

	Report.bSupported = Report.UnsupportedNodes.IsEmpty();
	return Report;
}
