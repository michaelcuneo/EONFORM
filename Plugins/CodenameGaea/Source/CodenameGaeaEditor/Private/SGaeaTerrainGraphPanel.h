#pragma once

#include "CoreMinimal.h"
#include "GaeaEditorGraph.h"
#include "GaeaTerrainEvaluator.h"
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

	void Construct(const FArguments& InArgs);
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Game-thread-only activity bridge used by async completion callbacks. */
	void SetEditorGraphActivity(EGaeaEditorGraphActivity Activity)
	{
		if (EditorGraph.IsValid()) EditorGraph->SetActivity(Activity);
	}

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
	bool EvaluateSelectedNodePreview();
	void RequestFinalEvaluationAsync();
	void RequestInspectionEvaluationAsync(const FGuid& NodeId);
	void StartNextAsyncEvaluation();
	void ClearInspectionPreview();
	void SyncOutputSettingsState();
	uint32 ComputeAutoPreviewHash() const;
	FText GetAssetText() const;
	FText GetStatusText() const;

	TStrongObjectPtr<UGaeaEditorGraph> EditorGraph;
	TStrongObjectPtr<UGaeaTerrainGraphAsset> CurrentAsset;
	TSharedPtr<FUICommandList> GraphCommands;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SVerticalBox> ParameterPanel;
	TWeakObjectPtr<UGaeaEditorGraphNode> SelectedNode;
	TWeakObjectPtr<UGaeaTerrainGraphAsset> LastOutputSettingsAsset;
	FSimpleDelegate OnEvaluated;
	FText StatusText;

	/**
	 * Persistent node outputs for interactive authoring. The pointer is replaced
	 * on asset switches so an obsolete worker can safely finish against its old
	 * cache without racing the newly opened graph.
	 */
	TSharedPtr<FGaeaTerrainEvaluationCache, ESPMode::ThreadSafe> IncrementalEvaluationCache =
		MakeShared<FGaeaTerrainEvaluationCache, ESPMode::ThreadSafe>();

	uint32 LastAutoPreviewHash = 0;
	float AutoPreviewPollAccumulator = 0.0f;
	FGuid LastPreviewNodeId;
	uint64 LastOutputSettingsRevision = 0;
	uint64 GraphEvaluationGeneration = 0;
	uint64 InspectionEvaluationGeneration = 0;
	FGuid PendingInspectionNodeId;
	bool bAutoPreviewInitialized = false;
	bool bAutoPreviewEvaluating = false;
	bool bFinalEvaluationPending = false;
	bool bLegacyEvaluateButtonHidden = false;
};
