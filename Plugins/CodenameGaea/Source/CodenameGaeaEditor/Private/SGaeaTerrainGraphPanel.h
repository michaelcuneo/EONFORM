#pragma once

#include "CoreMinimal.h"
#include "GaeaEditorGraph.h"
#include "GaeaTerrainRecipe.h"
#include "GraphEditor.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

using FGaeaGraphEditorEvents = SGraphEditor::FGraphEditorEvents;

class SVerticalBox;

class SGaeaTerrainGraphPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainGraphPanel) {}
		SLATE_EVENT(FSimpleDelegate, OnEvaluated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void BuildDefaultRecipeAndGraph();
	bool BuildRecipeFromEditorGraph(FGaeaTerrainRecipe& OutRecipe, FString& OutError) const;
	void OnGraphSelectionChanged(const TSet<UObject*>& NewSelection);
	void RebuildParameterPanel();

	FReply EvaluateGraph();
	FText GetStatusText() const;

	TStrongObjectPtr<UGaeaEditorGraph> EditorGraph;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<SVerticalBox> ParameterPanel;
	TWeakObjectPtr<UGaeaEditorGraphNode> SelectedNode;
	FSimpleDelegate OnEvaluated;
	FText StatusText;
};
