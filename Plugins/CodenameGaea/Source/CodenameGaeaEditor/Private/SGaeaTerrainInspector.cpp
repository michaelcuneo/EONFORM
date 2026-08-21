#include "SGaeaTerrainInspector.h"

#include "Engine/Texture2D.h"
#include "GaeaScalarField.h"
#include "SGaeaTerrainGraphPanel.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
	constexpr int32 TerrainInspectorPreviewResolution = 256;

	FString TerrainInspectorFieldUnitToString(EGaeaFieldUnit Unit)
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

	FString TerrainInspectorInterpolationToString(EGaeaInterpolation Interpolation)
	{
		switch (Interpolation)
		{
		case EGaeaInterpolation::Nearest: return TEXT("Nearest");
		case EGaeaInterpolation::Bilinear: return TEXT("Bilinear");
		default: return TEXT("Unknown");
		}
	}

	float TerrainInspectorSampleBilinear(const FGaeaScalarField& Field, double X, double Y)
	{
		const int32 Width = Field.Domain.Dimensions.X;
		const int32 Height = Field.Domain.Dimensions.Y;
		const double ClampedX = FMath::Clamp(X, 0.0, static_cast<double>(Width - 1));
		const double ClampedY = FMath::Clamp(Y, 0.0, static_cast<double>(Height - 1));
		const int32 X0 = FMath::FloorToInt(ClampedX);
		const int32 Y0 = FMath::FloorToInt(ClampedY);
		const int32 X1 = FMath::Min(X0 + 1, Width - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Height - 1);
		const float TX = static_cast<float>(ClampedX - static_cast<double>(X0));
		const float TY = static_cast<float>(ClampedY - static_cast<double>(Y0));

		const float A = FMath::Lerp(Field.AtInterior(X0, Y0), Field.AtInterior(X1, Y0), TX);
		const float B = FMath::Lerp(Field.AtInterior(X0, Y1), Field.AtInterior(X1, Y1), TX);
		return FMath::Lerp(A, B, TY);
	}
}

void SGaeaTerrainInspector::Construct(const FArguments& InArgs)
{
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.ImageSize = FVector2D(TerrainInspectorPreviewResolution, TerrainInspectorPreviewResolution);

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
			.FillHeight(0.54f)
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SGaeaTerrainGraphPanel)
				.OnEvaluated(FSimpleDelegate::CreateSP(this, &SGaeaTerrainInspector::RefreshFromRegistry))
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.46f)
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
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.Padding(4.0f)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
							[
								SNew(SBox)
								.WidthOverride(512.0f)
								.HeightOverride(512.0f)
								[
									SAssignNew(PreviewImage, SImage)
									.Image(&PreviewBrush)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SGaeaTerrainInspector::GetPreviewStatsText)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SGaeaTerrainInspector::GetEmptyStateText)
						]
					]
				]
			]
		]
	];

	RefreshFromRegistry();
}

void SGaeaTerrainInspector::RefreshFromRegistry()
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
		Names.Sort(FNameLexicalLess());
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
}

FReply SGaeaTerrainInspector::RefreshDataset()
{
	RefreshFromRegistry();
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

void SGaeaTerrainInspector::ClearPreview()
{
	PreviewTexture.Reset();
	PreviewBrush.SetResourceObject(nullptr);
	PreviewMinValue = 0.0f;
	PreviewMaxValue = 0.0f;
	PreviewMeanValue = 0.0f;
	PreviewStdDev = 0.0f;
	PreviewSampleCount = 0;
	if (PreviewImage.IsValid())
	{
		PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SGaeaTerrainInspector::RebuildPreview()
{
	ClearPreview();

	const FGaeaScalarField* Field = GetSelectedField();
	if (!Field || !Field->IsValid())
	{
		return;
	}

	PreviewMinValue = TNumericLimits<float>::Max();
	PreviewMaxValue = TNumericLimits<float>::Lowest();
	double Sum = 0.0;
	double SumSquares = 0.0;
	PreviewSampleCount = 0;

	const FIntPoint Dimensions = Field->Domain.Dimensions;
	for (int32 Y = 0; Y < Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Dimensions.X; ++X)
		{
			const float Value = Field->AtInterior(X, Y);
			PreviewMinValue = FMath::Min(PreviewMinValue, Value);
			PreviewMaxValue = FMath::Max(PreviewMaxValue, Value);
			Sum += static_cast<double>(Value);
			SumSquares += static_cast<double>(Value) * static_cast<double>(Value);
			++PreviewSampleCount;
		}
	}

	if (PreviewSampleCount <= 0)
	{
		ClearPreview();
		return;
	}

	PreviewMeanValue = static_cast<float>(Sum / static_cast<double>(PreviewSampleCount));
	const double Variance = FMath::Max(
		0.0,
		SumSquares / static_cast<double>(PreviewSampleCount)
			- static_cast<double>(PreviewMeanValue) * static_cast<double>(PreviewMeanValue));
	PreviewStdDev = static_cast<float>(FMath::Sqrt(Variance));

	const float Range = FMath::Max(PreviewMaxValue - PreviewMinValue, UE_SMALL_NUMBER);
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TerrainInspectorPreviewResolution * TerrainInspectorPreviewResolution);

	for (int32 PreviewY = 0; PreviewY < TerrainInspectorPreviewResolution; ++PreviewY)
	{
		const double SourceY = static_cast<double>(PreviewY)
			/ static_cast<double>(TerrainInspectorPreviewResolution - 1)
			* static_cast<double>(Dimensions.Y - 1);

		for (int32 PreviewX = 0; PreviewX < TerrainInspectorPreviewResolution; ++PreviewX)
		{
			const double SourceX = static_cast<double>(PreviewX)
				/ static_cast<double>(TerrainInspectorPreviewResolution - 1)
				* static_cast<double>(Dimensions.X - 1);
			const float Value = TerrainInspectorSampleBilinear(*Field, SourceX, SourceY);
			const float Normalized = FMath::Clamp((Value - PreviewMinValue) / Range, 0.0f, 1.0f);
			const uint8 Gray = static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
			Pixels[PreviewY * TerrainInspectorPreviewResolution + PreviewX] = FColor(Gray, Gray, Gray, 255);
		}
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(
		TerrainInspectorPreviewResolution,
		TerrainInspectorPreviewResolution,
		PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		ClearPreview();
		return;
	}

	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	PreviewTexture.Reset(Texture);
	PreviewBrush.SetResourceObject(Texture);
	if (PreviewImage.IsValid())
	{
		PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FText SGaeaTerrainInspector::GetSourceText() const
{
	if (!Snapshot.IsValid())
	{
		return FText::FromString(TEXT("No terrain dataset has been published yet."));
	}

	return FText::FromString(FString::Printf(
		TEXT("Source: %s   Revision: %llu   Fields: %d   Height scale: %.1f"),
		*Snapshot.SourceId.ToString(),
		static_cast<unsigned long long>(Snapshot.Revision),
		Snapshot.Dataset.NumScalarFields(),
		Snapshot.Metadata.HeightScale));
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
		TEXT("%s\nUnit: %s   Interpolation: %s\nResolution: %d x %d   Border: %d\nWorld: [%.1f, %.1f] to [%.1f, %.1f]\nCell size: %.2f x %.2f\nPreview: %d x %d, bilinear"),
		*Field->Descriptor.Name.ToString(),
		*TerrainInspectorFieldUnitToString(Field->Descriptor.Unit),
		*TerrainInspectorInterpolationToString(Field->Descriptor.Interpolation),
		Domain.Dimensions.X,
		Domain.Dimensions.Y,
		Domain.BorderSamples,
		Domain.WorldMin.X,
		Domain.WorldMin.Y,
		Domain.WorldMax.X,
		Domain.WorldMax.Y,
		CellSize.X,
		CellSize.Y,
		TerrainInspectorPreviewResolution,
		TerrainInspectorPreviewResolution));
}

FText SGaeaTerrainInspector::GetPreviewStatsText() const
{
	if (PreviewSampleCount <= 0)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(FString::Printf(
		TEXT("Min %.6g   Max %.6g   Mean %.6g   StdDev %.6g   Samples %lld"),
		PreviewMinValue,
		PreviewMaxValue,
		PreviewMeanValue,
		PreviewStdDev,
		static_cast<long long>(PreviewSampleCount)));
}

FText SGaeaTerrainInspector::GetEmptyStateText() const
{
	if (!Snapshot.IsValid())
	{
		return FText::FromString(TEXT("Generate terrain, then press Refresh to inspect the latest published dataset."));
	}
	if (!GetSelectedField())
	{
		return FText::FromString(TEXT("Select a terrain field to inspect it."));
	}
	return FText::GetEmpty();
}
