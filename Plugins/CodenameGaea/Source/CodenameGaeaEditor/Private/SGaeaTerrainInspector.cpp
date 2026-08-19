#include "SGaeaTerrainInspector.h"

#include "GaeaScalarField.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
	FString FieldUnitToString(EGaeaFieldUnit Unit)
	{
		switch (Unit)
		{
		case EGaeaFieldUnit::Unitless: return TEXT("Unitless");
		case EGaeaFieldUnit::Normalized: return TEXT("Normalized");
		case EGaeaFieldUnit::Centimeters: return TEXT("Centimeters");
		case EGaeaFieldUnit::Meters: return TEXT("Meters");
		case EGaeaFieldUnit::Degrees: return TEXT("Degrees");
		case EGaeaFieldUnit::Celsius: return TEXT("Celsius");
		default: return TEXT("Unknown");
		}
	}

	FString InterpolationToString(EGaeaInterpolation Interpolation)
	{
		switch (Interpolation)
		{
		case EGaeaInterpolation::Nearest: return TEXT("Nearest");
		case EGaeaInterpolation::Bilinear: return TEXT("Bilinear");
		default: return TEXT("Unknown");
		}
	}
}

void SGaeaTerrainInspector::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.Padding(10.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SGaeaTerrainInspector::GetSourceText)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Refresh")))
					.OnClicked(this, &SGaeaTerrainInspector::RefreshDataset)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(0.28f)
				[
					SNew(SBorder)
					.Padding(6.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Terrain Fields")))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SAssignNew(FieldListView, SListView<TSharedPtr<FName>>)
							.ListItemsSource(&FieldItems)
							.OnGenerateRow(this, &SGaeaTerrainInspector::GenerateFieldRow)
							.OnSelectionChanged(this, &SGaeaTerrainInspector::OnFieldSelectionChanged)
						]
					]
				]

				+ SSplitter::Slot()
				.Value(0.72f)
				[
					SNew(SBorder)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(this, &SGaeaTerrainInspector::GetFieldMetadataText)
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SBorder)
							.Padding(4.0f)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
							[
								SAssignNew(PreviewGrid, SUniformGridPanel)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SGaeaTerrainInspector::GetEmptyStateText)
						]
					]
				]
			]
		]
	];

	RefreshDataset();
}

FReply SGaeaTerrainInspector::RefreshDataset()
{
	FGaeaTerrainDatasetSnapshot NewSnapshot;
	if (FGaeaTerrainDatasetRegistry::GetLatest(NewSnapshot))
	{
		Snapshot = MoveTemp(NewSnapshot);
	}
	else
	{
		Snapshot = FGaeaTerrainDatasetSnapshot();
	}

	FieldItems.Reset();
	SelectedFieldName = NAME_None;

	if (Snapshot.IsValid())
	{
		TArray<FName> Names;
		Snapshot.Dataset.GetScalarFieldNames(Names);
		for (const FName Name : Names)
		{
			FieldItems.Add(MakeShared<FName>(Name));
		}
	}

	if (FieldListView.IsValid())
	{
		FieldListView->RequestListRefresh();
		if (!FieldItems.IsEmpty())
		{
			FieldListView->SetSelection(FieldItems[0]);
		}
	}

	RebuildPreview();
	return FReply::Handled();
}

TSharedRef<ITableRow> SGaeaTerrainInspector::GenerateFieldRow(
	TSharedPtr<FName> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(FText::FromName(Item.IsValid() ? *Item : NAME_None))
	];
}

void SGaeaTerrainInspector::OnFieldSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	SelectedFieldName = Item.IsValid() ? *Item : NAME_None;
	RebuildPreview();
}

const FGaeaScalarField* SGaeaTerrainInspector::GetSelectedField() const
{
	if (!Snapshot.IsValid() || SelectedFieldName.IsNone())
	{
		return nullptr;
	}
	return Snapshot.Dataset.FindScalarField(SelectedFieldName);
}

void SGaeaTerrainInspector::RebuildPreview()
{
	if (!PreviewGrid.IsValid())
	{
		return;
	}

	PreviewGrid->ClearChildren();
	const FGaeaScalarField* Field = GetSelectedField();
	if (!Field || !Field->IsValid())
	{
		return;
	}

	float MinValue = TNumericLimits<float>::Max();
	float MaxValue = TNumericLimits<float>::Lowest();
	for (const float Value : Field->Values)
	{
		MinValue = FMath::Min(MinValue, Value);
		MaxValue = FMath::Max(MaxValue, Value);
	}
	const float Range = FMath::Max(MaxValue - MinValue, UE_SMALL_NUMBER);

	constexpr int32 PreviewResolution = 32;
	const FIntPoint Dimensions = Field->Domain.Dimensions;
	for (int32 PreviewY = 0; PreviewY < PreviewResolution; ++PreviewY)
	{
		const int32 SourceY = FMath::RoundToInt(
			static_cast<double>(PreviewY) / static_cast<double>(PreviewResolution - 1)
			* static_cast<double>(Dimensions.Y - 1));

		for (int32 PreviewX = 0; PreviewX < PreviewResolution; ++PreviewX)
		{
			const int32 SourceX = FMath::RoundToInt(
				static_cast<double>(PreviewX) / static_cast<double>(PreviewResolution - 1)
				* static_cast<double>(Dimensions.X - 1));

			const float Value = Field->AtInterior(SourceX, SourceY);
			const float Normalized = FMath::Clamp((Value - MinValue) / Range, 0.0f, 1.0f);

			PreviewGrid->AddSlot(PreviewX, PreviewY)
			[
				SNew(SBox)
				.WidthOverride(8.0f)
				.HeightOverride(8.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(Normalized, Normalized, Normalized, 1.0f))
				]
			];
		}
	}
}

FText SGaeaTerrainInspector::GetSourceText() const
{
	if (!Snapshot.IsValid())
	{
		return FText::FromString(TEXT("No terrain dataset has been published yet."));
	}

	return FText::FromString(FString::Printf(
		TEXT("Source: %s   Revision: %llu   Fields: %d"),
		*Snapshot.SourceId.ToString(),
		static_cast<unsigned long long>(Snapshot.Revision),
		Snapshot.Dataset.NumScalarFields()));
}

FText SGaeaTerrainInspector::GetFieldMetadataText() const
{
	const FGaeaScalarField* Field = GetSelectedField();
	if (!Field)
	{
		return FText::FromString(TEXT("Select a terrain field to inspect it."));
	}

	const FGaeaGridDomain& Domain = Field->Domain;
	const FVector2d CellSize = Domain.GetCellSize();
	return FText::FromString(FString::Printf(
		TEXT("%s\nUnit: %s   Interpolation: %s\nResolution: %d x %d   Border: %d\nWorld: [%.1f, %.1f] to [%.1f, %.1f]\nCell size: %.2f x %.2f"),
		*Field->Descriptor.Name.ToString(),
		*FieldUnitToString(Field->Descriptor.Unit),
		*InterpolationToString(Field->Descriptor.Interpolation),
		Domain.Dimensions.X,
		Domain.Dimensions.Y,
		Domain.BorderSamples,
		Domain.WorldMin.X,
		Domain.WorldMin.Y,
		Domain.WorldMax.X,
		Domain.WorldMax.Y,
		CellSize.X,
		CellSize.Y));
}

FText SGaeaTerrainInspector::GetEmptyStateText() const
{
	if (!Snapshot.IsValid())
	{
		return FText::FromString(TEXT("Generate terrain, then press Refresh to inspect the latest published dataset."));
	}
	return FText::GetEmpty();
}
