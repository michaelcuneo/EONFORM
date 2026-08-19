#include "SGaeaTerrainGraphPanel.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "InputCoreTypes.h"

FReply SGaeaTerrainGraphPanel::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete && CanDeleteSelectedNodes())
	{
		DeleteSelectedNodes();
		return FReply::Handled();
	}

	if (!GraphCommands.IsValid())
	{
		GraphCommands = MakeShared<FUICommandList>();
		GraphCommands->MapAction(
			FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(SharedThis(this), &SGaeaTerrainGraphPanel::DeleteSelectedNodes),
			FCanExecuteAction::CreateSP(SharedThis(this), &SGaeaTerrainGraphPanel::CanDeleteSelectedNodes));
	}

	if (GraphCommands->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

bool SGaeaTerrainGraphPanel::CanDeleteSelectedNodes() const
{
	if (!GraphEditor.IsValid())
	{
		return false;
	}

	for (UObject* Object : GraphEditor->GetSelectedNodes())
	{
		const UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node && Node->CanUserDeleteNode())
		{
			return true;
		}
	}

	return false;
}

void SGaeaTerrainGraphPanel::DeleteSelectedNodes()
{
	if (!GraphEditor.IsValid() || !EditorGraph.IsValid())
	{
		return;
	}

	const TSet<UObject*> Selection = GraphEditor->GetSelectedNodes();
	GraphEditor->ClearSelectionSet();

	int32 DeletedCount = 0;
	for (UObject* Object : Selection)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (!Node || !Node->CanUserDeleteNode())
		{
			continue;
		}

		if (EditorGraph->RemoveNode(Node, true, false))
		{
			++DeletedCount;
		}
	}

	if (DeletedCount > 0)
	{
		EditorGraph->NotifyGraphChanged();
	}

	SelectedNode.Reset();
	RebuildParameterPanel();
	StatusText = DeletedCount > 0
		? FText::FromString(FString::Printf(TEXT("Deleted %d graph node(s)."), DeletedCount))
		: FText::FromString(TEXT("No deletable graph nodes were selected."));
}
