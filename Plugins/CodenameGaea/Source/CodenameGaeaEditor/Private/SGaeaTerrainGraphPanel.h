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

	void Construct(const FArguments& InArgs);
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * Synchronously evaluates the graph currently displayed in the EONFORM graph panel,
	 * publishes the authoritative Terrain Output snapshot, and refreshes the inspector.
	 * Generate Terrain uses this as a correctness barrier; it must never depend on an
	 * optional/stale automatic preview having completed first.
	 */
	static bool EvaluateActiveGraphAndPublish(FString& OutError);

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
	void RequestAutoPreviewEvaluation();
	void RequestSelectedNodePreview(const FGuid& NodeId);
	void StartAutoPreviewEvaluation();
	void SyncOutputSettingsState();
	uint32 ComputeAutoPreviewHash() const;
	FText GetAssetText() const;
	FText GetStatusText() const;

	static TWeakPtr<SGaeaTerrainGraphPanel> ActivePanel;

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

	uint32 LastAutoPreviewHash = 0;
	float AutoPreviewPollAccumulator = 0.0f;
	FGuid LastPreviewNodeId;
	FGuid PendingSelectedPreviewNodeId;
	FGuid ActiveAutoPreviewNodeId;
	uint64 LastOutputSettingsRevision = 0;
	uint64 AutoPreviewRequestSerial = 0;
	bool bAutoPreviewInitialized = false;
	bool bAutoPreviewEvaluating = false;
	bool bAutoPreviewRestartPending = false;
	bool bFinalAutoPreviewPending = false;
	bool bLegacyEvaluateButtonHidden = false;
};
