#include "GaeaEditorGraph.h"

#include "GaeaTerrainRecipe.h"

namespace
{
	const FName TerrainPinCategory(TEXT("GaeaTerrain"));
}

void UGaeaEditorGraphNode::Initialize(const FGuid& InRecipeNodeId, FName InRecipeNodeType)
{
	RecipeNodeId = InRecipeNodeId;
	RecipeNodeType = InRecipeNodeType;
}

void UGaeaEditorGraphNode::AllocateDefaultPins()
{
	FCreatePinParams PinParams;

	if (RecipeNodeType == GaeaTerrainNodeTypes::SourceDataset)
	{
		CreatePin(EGPD_Output, TerrainPinCategory, TEXT("Terrain"), PinParams);
		return;
	}

	if (RecipeNodeType == GaeaTerrainNodeTypes::HydraulicErosion)
	{
		CreatePin(EGPD_Input, TerrainPinCategory, TEXT("Terrain"), PinParams);
		CreatePin(EGPD_Output, TerrainPinCategory, TEXT("Terrain"), PinParams);
	}
}

FText UGaeaEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (RecipeNodeType == GaeaTerrainNodeTypes::SourceDataset)
	{
		return FText::FromString(TEXT("Source Dataset"));
	}

	if (RecipeNodeType == GaeaTerrainNodeTypes::HydraulicErosion)
	{
		return FText::FromString(TEXT("Hydraulic Erosion"));
	}

	return FText::FromName(RecipeNodeType);
}

FText UGaeaEditorGraphNode::GetTooltipText() const
{
	if (RecipeNodeType == GaeaTerrainNodeTypes::SourceDataset)
	{
		return FText::FromString(TEXT("Uses the latest published terrain dataset as the graph input."));
	}

	if (RecipeNodeType == GaeaTerrainNodeTypes::HydraulicErosion)
	{
		return FText::FromString(TEXT("Runs runtime-safe hydraulic erosion and publishes Height, Wear, Deposits, and Flow in the output dataset."));
	}

	return FText::GetEmpty();
}

const FPinConnectionResponse UGaeaEditorGraphSchema::CanCreateConnection(
	const UEdGraphPin* A,
	const UEdGraphPin* B) const
{
	if (!A || !B)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid terrain graph pin."));
	}

	if (A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("A node cannot connect to itself."));
	}

	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Connect an output pin to an input pin."));
	}

	if (A->PinType.PinCategory != TerrainPinCategory || B->PinType.PinCategory != TerrainPinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Terrain pins can only connect to terrain pins."));
	}

	const UEdGraphPin* InputPin = A->Direction == EGPD_Input ? A : B;
	if (!InputPin->LinkedTo.IsEmpty())
	{
		return A == InputPin
			? FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, TEXT("Replace the existing terrain input."))
			: FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, TEXT("Replace the existing terrain input."));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
}

void UGaeaEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// Node creation is introduced after the first tested visual/evaluation slice.
}
