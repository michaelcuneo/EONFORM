#include "EonformTerrainGraphAsset.h"

void UEonformTerrainGraphAsset::PostLoad()
{
	Super::PostLoad();
	MigrateLegacyConnections();
}

void UEonformTerrainGraphAsset::MigrateLegacyConnections()
{
	for (FEonformTerrainConnection& Connection : Recipe.Connections)
	{
		// Current graph nodes display their primary result as Out, but the editor
		// keeps the stable internal terrain-channel name so authored graphs can be
		// reconstructed consistently across the Gaea 1 -> Gaea 2 cleanup.
		if (Connection.FromOutput == TEXT("Out"))
		{
			Connection.FromOutput = TEXT("Terrain");
		}

		const FEonformTerrainNode* Destination = Recipe.FindNode(Connection.ToNode);
		if (!Destination)
		{
			continue;
		}

		if (Destination->Type == EonformTerrainNodeTypes::Combine)
		{
			if (Connection.ToInput == TEXT("Primary")) Connection.ToInput = TEXT("Input1");
			else if (Connection.ToInput == TEXT("Secondary")) Connection.ToInput = TEXT("Input2");
		}
		else if (Destination->Type == EonformTerrainNodeTypes::HydraulicErosion)
		{
			if (Connection.ToInput == TEXT("Mask")) Connection.ToInput = TEXT("Area");
		}
	}
}

const FEonformTerrainNodeLayout* UEonformTerrainGraphAsset::FindLayout(const FGuid& NodeId) const
{
	return NodeLayout.FindByPredicate([&NodeId](const FEonformTerrainNodeLayout& Layout)
	{
		return Layout.NodeId == NodeId;
	});
}

void UEonformTerrainGraphAsset::SetLayout(const FGuid& NodeId, const FVector2D& Position)
{
	if (FEonformTerrainNodeLayout* Existing = NodeLayout.FindByPredicate([&NodeId](const FEonformTerrainNodeLayout& Layout)
	{
		return Layout.NodeId == NodeId;
	}))
	{
		Existing->Position = Position;
		return;
	}

	FEonformTerrainNodeLayout Layout;
	Layout.NodeId = NodeId;
	Layout.Position = Position;
	NodeLayout.Add(MoveTemp(Layout));
}
