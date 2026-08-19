#pragma once

#include "CoreMinimal.h"
#include "GaeaEditorGraph.h"
#include "GaeaTerrainGraphAsset.h"
#include "GaeaTerrainRecipe.h"
#include "GraphEditor.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class FUICommandList;
class SBox;
class SVerticalBox;

class SGaeaTerrainGraphPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainGraphPanel) {}
		SLATE_EVENT(FSimpleDelegate, OnEvaluated)
	SLATE_END_ARGS()

	SGaeaTerrainGraphPanel();

	void Construct(const FArguments& InArgs);
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildDefaultRecipeAndGraph();
	void BuildEditorGraphFromRecipe(const FGaeaTerrainRecipe& Recipe, const UGaeaTerrainGraphAsset* Asset = nullptr);
	TSharedRef<SWidget> CreateGraphEditorWidget();

	bool BuildRecipeFromEditorGraph(FGaeaTerrainRecipe& OutRecipe, FString& OutError) const;
	bool WriteEditorGraphToAsset(UGaeaTerrainGraphAsset& Asset, FString& OutError) const;
	void LoadAsset(UGaeaTerrainGraphAsset& Asset);
	bool SaveCurrentAsset(bool bPromptForCheckout);
	UGaeaTerrainGraphAsset* CreateAssetFromCurrentGraph();

	void DeleteSelectedNodes();
	bool CanDeleteSelectedNodes() const;
	void OnGraphSelectionChanged(const TSet<UObject*>& NewSelection);
	void RebuildParameterPanel();

	FReply NewGraphAsset();
	FReply OpenGraphAsset();
	FReply SaveGraphAsset();
	FReply EvaluateGraph();
	FText GetAssetText() const;
	FText GetStatusText() const;

	TStrongObjectPtr<UGaeaEditorGraph> EditorGraph;
	TStrongObjectPtr<UGaeaTerrainGraphAsset> CurrentAsset;
	TSharedPtr<FUICommandList> GraphCommands;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SVerticalBox> ParameterPanel;
	TWeakObjectPtr<UGaeaEditorGraphNode> SelectedNode;
	FSimpleDelegate OnEvaluated;
	FText StatusText;
};
