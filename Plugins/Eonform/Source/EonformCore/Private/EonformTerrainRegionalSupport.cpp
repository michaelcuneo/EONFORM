#include "EonformTerrainRegionalSupport.h"

#include "EonformAngleNode.h"
#include "EonformBlurNode.h"
#include "EonformCurvatureNode.h"
#include "EonformDenoiseNode.h"
#include "EonformSharpenNode.h"
#include "EonformSlopeNode.h"

namespace
{
	bool AuditNode(
		const FEonformTerrainNode& Node,
		const FIntPoint& ReferenceResolution,
		int32& OutLocalBorderSamples,
		FString& OutReason)
	{
		OutLocalBorderSamples = 0;
		OutReason.Reset();

		if (Node.Type == EonformTerrainNodeTypes::PerlinNoise
			|| Node.Type == EonformTerrainNodeTypes::Voronoi
			|| Node.Type == EonformTerrainNodeTypes::Constant
			|| Node.Type == EonformTerrainNodeTypes::Combine
			|| Node.Type == EonformTerrainNodeTypes::Clamp
			|| Node.Type == EonformTerrainNodeTypes::Invert
			|| Node.Type == EonformTerrainNodeTypes::Threshold
			|| Node.Type == EonformTerrainNodeTypes::Terrace)
		{
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Blur)
		{
			const int32 Radius = EonformBlurNode::ResolveRadiusSamples(Node, ReferenceResolution);
			if (Radius == INDEX_NONE)
			{
				OutReason = TEXT("regional Blur with non-zero Radius requires a valid full-world reference resolution");
				return false;
			}
			OutLocalBorderSamples = Radius;
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Denoise)
		{
			OutLocalBorderSamples = EonformDenoiseNode::RequiredBorderSamples(Node);
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Sharpen)
		{
			OutLocalBorderSamples = EonformSharpenNode::RequiredBorderSamples(Node);
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Slope)
		{
			OutLocalBorderSamples = EonformSlopeNode::RequiredBorderSamples();
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Curvature)
		{
			OutLocalBorderSamples = EonformCurvatureNode::RequiredBorderSamples();
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Angle)
		{
			OutLocalBorderSamples = EonformAngleNode::RequiredBorderSamples();
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Ridge)
		{
			// Ridge resolves displaced samples lazily in the full-world reference
			// lattice and shares its exact whole-field reduction through the
			// generation-plan summary cache, so it consumes no external halo.
			return true;
		}

		if (Node.Type == EonformTerrainNodeTypes::Mountain)
		{
			const FName Style = Node.GetName(TEXT("Style"), TEXT("Eroded"));
			const FName Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
			if (Style == TEXT("Basic") && Bulk == TEXT("Medium"))
			{
				return true;
			}
			OutReason = TEXT("regional Mountain currently requires Style=Basic and Bulk=Medium; erosion, strata, and global bulk reductions are not region-equivalent yet");
			return false;
		}

		if (Node.Type == EonformTerrainNodeTypes::AutoLevel || Node.Type == EonformTerrainNodeTypes::Equalize)
		{
			OutReason = TEXT("global normalization requires cached world statistics");
			return false;
		}

		if (Node.Type == EonformTerrainNodeTypes::HydraulicErosion
			|| Node.Type == EonformTerrainNodeTypes::Rivers
			|| Node.Type == EonformTerrainNodeTypes::FlowMap
			|| Node.Type == EonformTerrainNodeTypes::FlowMapClassic)
		{
			OutReason = TEXT("hydrology requires macro/global drainage state before independent regional refinement");
			return false;
		}

		OutReason = FString::Printf(
			TEXT("node type '%s' has not yet been proven region-equivalent"),
			*Node.Type.ToString());
		return false;
	}
}

FString FEonformTerrainRegionalSupportReport::Describe() const
{
	if (bSupported) return TEXT("All reachable terrain nodes support independent regional evaluation.");
	if (Reasons.IsEmpty()) return TEXT("The graph contains nodes that do not yet support independent regional evaluation.");
	return FString::Join(Reasons, TEXT("; "));
}

FEonformTerrainRegionalSupportReport FEonformTerrainRegionalSupport::Analyze(const FEonformTerrainRecipe& Recipe)
{
	return Analyze(Recipe, FIntPoint::ZeroValue);
}

FEonformTerrainRegionalSupportReport FEonformTerrainRegionalSupport::Analyze(
	const FEonformTerrainRecipe& Recipe,
	const FIntPoint& ReferenceResolution)
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

	TMap<FGuid, int32> LocalBorders;
	for (const FEonformTerrainNode& Node : Recipe.Nodes)
	{
		if (!Reachable.Contains(Node.Id)) continue;

		int32 LocalBorderSamples = 0;
		FString Reason;
		if (!AuditNode(Node, ReferenceResolution, LocalBorderSamples, Reason))
		{
			Report.UnsupportedNodes.Add(Node.Id);
			Report.Reasons.Add(FString::Printf(TEXT("%s: %s"), *Node.Type.ToString(), *Reason));
			continue;
		}
		LocalBorders.Add(Node.Id, LocalBorderSamples);
	}

	if (!Report.UnsupportedNodes.IsEmpty())
	{
		Report.bSupported = false;
		return Report;
	}

	TMap<FGuid, int32> RequiredBorderCache;
	TSet<FGuid> Visiting;
	TFunction<bool(const FGuid&, int32&)> ResolveRequiredBorder = [&](const FGuid& NodeId, int32& OutRequiredBorder)
	{
		if (const int32* Cached = RequiredBorderCache.Find(NodeId))
		{
			OutRequiredBorder = *Cached;
			return true;
		}
		if (Visiting.Contains(NodeId))
		{
			Report.UnsupportedNodes.AddUnique(NodeId);
			Report.Reasons.AddUnique(TEXT("regional dependency analysis encountered a cycle"));
			return false;
		}

		const int32* LocalBorder = LocalBorders.Find(NodeId);
		if (!LocalBorder)
		{
			Report.UnsupportedNodes.AddUnique(NodeId);
			Report.Reasons.AddUnique(TEXT("regional dependency analysis could not resolve a reachable node"));
			return false;
		}

		Visiting.Add(NodeId);
		int32 MaxUpstreamBorder = 0;
		for (const FEonformTerrainConnection& Connection : Recipe.Connections)
		{
			if (Connection.ToNode != NodeId) continue;
			int32 UpstreamBorder = 0;
			if (!ResolveRequiredBorder(Connection.FromNode, UpstreamBorder))
			{
				Visiting.Remove(NodeId);
				return false;
			}
			MaxUpstreamBorder = FMath::Max(MaxUpstreamBorder, UpstreamBorder);
		}
		Visiting.Remove(NodeId);

		OutRequiredBorder = MaxUpstreamBorder + *LocalBorder;
		RequiredBorderCache.Add(NodeId, OutRequiredBorder);
		return true;
	};

	int32 OutputBorder = 0;
	if (!ResolveRequiredBorder(Recipe.OutputNode, OutputBorder))
	{
		Report.bSupported = false;
		return Report;
	}

	Report.RequiredBorderSamples = OutputBorder;
	Report.bSupported = true;
	return Report;
}
