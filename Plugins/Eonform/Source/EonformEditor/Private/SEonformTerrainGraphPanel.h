#pragma once

#include "CoreMinimal.h"
#include "EonformEditorGraph.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainGraphAsset.h"
#include "EonformTerrainRecipe.h"
#include "GraphEditor.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class FUICommandList;
class SBox;
class SVerticalBox;

class SEonformTerrainGraphPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEonformTerrainGraphPanel) {}
		SLATE_EVENT(FSimpleDelegate, OnEvaluated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Game-thread-only activity bridge used by async completion callbacks. */
	void SetEditorGraphActivity(EEonformEditorGraphActivity Activity)
	{
		if (EditorGraph.IsValid()) EditorGraph->SetActivity(Activity);
	}

	UEonformEditorGraph* GetEditorGraphForActivity() const
	{
		return EditorGraph.Get();
	}

private:
	void BuildDefaultRecipeAndGraph();
	void BuildEditorGraphFromRecipe(const FEonformTerrainRecipe& Recipe, const UEonformTerrainGraphAsset* Asset = nullptr);
	TSharedRef<SWidget> CreateGraphEditorWidget();

	bool BuildRecipeFromEditorGraph(FEonformTerrainRecipe& OutRecipe, FString& OutError) const;
	bool WriteEditorGraphToAsset(UEonformTerrainGraphAsset& Asset, FString& OutError) const;
	void LoadAsset(UEonformTerrainGraphAsset& Asset);
	bool SaveCurrentAsset(bool bPromptForCheckout);
	UEonformTerrainGraphAsset* CreateAssetFromCurrentGraph();

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

public:
	/** Internal graph object exposed only so game-thread async activity callbacks can drive Slate feedback. */
	TStrongObjectPtr<UEonformEditorGraph> EditorGraph;

private:
	TStrongObjectPtr<UEonformTerrainGraphAsset> CurrentAsset;
	TSharedPtr<FUICommandList> GraphCommands;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SVerticalBox> ParameterPanel;
	TWeakObjectPtr<UEonformEditorGraphNode> SelectedNode;
	TWeakObjectPtr<UEonformTerrainGraphAsset> LastOutputSettingsAsset;
	FSimpleDelegate OnEvaluated;
	FText StatusText;

	TSharedPtr<FEonformTerrainEvaluationCache, ESPMode::ThreadSafe> IncrementalEvaluationCache =
		MakeShared<FEonformTerrainEvaluationCache, ESPMode::ThreadSafe>();

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
