#include "GaeaTerrainGraphNode.h"

#include "GaeaEditorGraph.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SGaeaTerrainGraphNode::Construct(const FArguments& InArgs, UEdGraphNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGaeaTerrainGraphNode::UpdateGraphNode()
{
	InputPins.Reset();
	OutputPins.Reset();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Graph.Node.Body"))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Graph.Node.TitleBackground"))
				.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f, 1.0f))
				.Padding(FMargin(8.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(GraphNode ? GraphNode->GetNodeTitle(ENodeTitleType::FullTitle) : FText::GetEmpty())
					.ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreateNodeContentArea()
			]
		]
	];

	CreatePinWidgets();
}

TSharedPtr<SGraphPin> SGaeaTerrainGraphNode::CreatePinWidget(UEdGraphPin* Pin) const
{
	if (!Pin)
	{
		return nullptr;
	}

	TSharedPtr<SGraphPin> PinWidget = SGraphNode::CreatePinWidget(Pin);
	if (!PinWidget.IsValid())
	{
		PinWidget = SNew(SGraphPin, Pin);
	}

	PinWidget->EnableDragAndDrop(true);
	return PinWidget;
}

TSharedPtr<SGraphNode> FGaeaTerrainGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (!Cast<UGaeaEditorGraphNode>(InNode))
	{
		return nullptr;
	}

	return SNew(SGaeaTerrainGraphNode, InNode);
}
