#include "GaeaTerrainGraphAsset.h"

void UGaeaTerrainGraphAsset::PostLoad()
{
	Super::PostLoad();
	MigrateLegacyConnections();
}

void UGaeaTerrainGraphAsset::MigrateLegacyConnections()
{
	for (FGaeaTerrainConnection& Connection : Recipe.Connections)
	{
		// Current graph nodes display their primary result as Out, but the editor
		// keeps the stable internal terrain-channel name so authored graphs can be
		// reconstructed consistently across the Gaea 1 -> Gaea 2 cleanup.
		if (Connection.FromOutput == TEXT("Out"))
		{
			Connection.FromOutput = TEXT("Terrain");
		}

		const FGaeaTerrainNode* Destination = Recipe.FindNode(Connection.ToNode);
		if (!Destination)
		{
			continue;
		}

		if (Destination->Type == GaeaTerrainNodeTypes::Combine)
		{
			if (Connection.ToInput == TEXT("Primary")) Connection.ToInput = TEXT("Input1");
			else if (Connection.ToInput == TEXT("Secondary")) Connection.ToInput = TEXT("Input2");
		}
		else if (Destination->Type == GaeaTerrainNodeTypes::HydraulicErosion)
		{
			if (Connection.ToInput == TEXT("Mask")) Connection.ToInput = TEXT("Area");
		}
	}
}

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
