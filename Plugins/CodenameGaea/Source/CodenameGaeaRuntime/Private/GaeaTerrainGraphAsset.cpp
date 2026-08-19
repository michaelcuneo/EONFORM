#include "GaeaTerrainGraphAsset.h"

const FGaeaTerrainNodeLayout* UGaeaTerrainGraphAsset::FindLayout(const FGuid& NodeId) const
{
	return NodeLayout.FindByPredicate([&NodeId](const FGaeaTerrainNodeLayout& Layout)
	{
		return Layout.NodeId == NodeId;
	});
}

void UGaeaTerrainGraphAsset::SetLayout(const FGuid& NodeId, const FVector2D& Position)
{
	if (FGaeaTerrainNodeLayout* Existing = NodeLayout.FindByPredicate([&NodeId](const FGaeaTerrainNodeLayout& Layout)
	{
		return Layout.NodeId == NodeId;
	}))
	{
		Existing->Position = Position;
		return;
	}

	FGaeaTerrainNodeLayout Layout;
	Layout.NodeId = NodeId;
	Layout.Position = Position;
	NodeLayout.Add(MoveTemp(Layout));
}
