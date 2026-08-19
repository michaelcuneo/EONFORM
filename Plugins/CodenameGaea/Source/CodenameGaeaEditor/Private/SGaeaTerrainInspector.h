#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "Widgets/SCompoundWidget.h"

class SUniformGridPanel;
template<typename ItemType> class SListView;

class SGaeaTerrainInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainInspector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply RefreshDataset();
	TSharedRef<ITableRow> GenerateFieldRow(TSharedPtr<FName> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnFieldSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo);
	void RebuildPreview();

	const FGaeaScalarField* GetSelectedField() const;
	FText GetSourceText() const;
	FText GetFieldMetadataText() const;
	FText GetEmptyStateText() const;

	FGaeaTerrainDatasetSnapshot Snapshot;
	TArray<TSharedPtr<FName>> FieldItems;
	FName SelectedFieldName = NAME_None;

	TSharedPtr<SListView<TSharedPtr<FName>>> FieldListView;
	TSharedPtr<SUniformGridPanel> PreviewGrid;
};
