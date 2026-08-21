#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SGaeaTerrainMeshPreview;
class SImage;
class SWidget;
class UTexture2D;

class SGaeaTerrainInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainInspector) {}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, OutputPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void RefreshFromRegistry();

private:
	FReply RefreshDataset();
	TSharedRef<ITableRow> GenerateFieldRow(TSharedPtr<FName> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnFieldSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo);
	void RebuildPreview();
	void ClearPreview();

	const FGaeaScalarField* GetSelectedField() const;
	FText GetSourceText() const;
	FText GetFieldMetadataText() const;
	FText GetPreviewStatsText() const;
	FText GetEmptyStateText() const;

	FGaeaTerrainDatasetSnapshot Snapshot;
	TArray<TSharedPtr<FName>> FieldItems;
	FName SelectedFieldName = NAME_None;

	TSharedPtr<SListView<TSharedPtr<FName>>> FieldListView;
	TSharedPtr<SImage> PreviewImage;
	TSharedPtr<SGaeaTerrainMeshPreview> MeshPreview;
	FSlateBrush PreviewBrush;
	TStrongObjectPtr<UTexture2D> PreviewTexture;

	float PreviewMinValue = 0.0f;
	float PreviewMaxValue = 0.0f;
	float PreviewMeanValue = 0.0f;
	float PreviewStdDev = 0.0f;
	int64 PreviewSampleCount = 0;
	int64 PreviewNonFiniteCount = 0;
};
