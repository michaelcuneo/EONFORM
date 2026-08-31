#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SUniformGridPanel;

class SEonformTerrainRegionGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEonformTerrainRegionGrid) {}
		SLATE_ARGUMENT(FName, SourceId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Rebuild();
	FReply HandleRegionClicked(FIntPoint Coordinate);
	FReply RegenerateSelected();
	FReply UnloadSelected();
	FReply SelectActorsForSelected();
	FReply ClearSelection();
	void ApplySelectionToEditor();
	void SyncSelectionFromEditor();
	void AddRectangularSelection(const FIntPoint& A, const FIntPoint& B, bool bAdditive);
	TArray<FIntPoint> GetSelectedRegions() const;
	FText GetLayoutSummaryText() const;
	FText GetSelectionText() const;
	FText GetActionStatusText() const;
	bool HasSelection() const;

	uint64 LastChangeSerial = 0;
	uint64 LastPlanningSignature = 0;
	FIntPoint LatestGridDimensions = FIntPoint::ZeroValue;
	FIntPoint LatestComponentDimensions = FIntPoint::ZeroValue;
	TSet<FIntPoint> SelectedRegions;
	TSet<FIntPoint> LastEditorRegionSelection;
	FIntPoint SelectionAnchor = FIntPoint::ZeroValue;
	bool bHasSelectionAnchor = false;
	FString LayoutSummary;
	FString ActionStatus;
	TSharedPtr<SUniformGridPanel> Grid;
};
