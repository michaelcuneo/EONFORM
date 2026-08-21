#include "SGaeaTerrainInspector.h"

#include "Engine/Texture2D.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainFieldNames.h"
#include "SGaeaTerrainGraphPanel.h"
#include "SGaeaTerrainMeshPreview.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
	constexpr int32 TerrainInspectorPreviewResolution = 256;

	enum class ETerrainInspectorPalette : uint8
	{
		Grayscale,
		Height,
		Slope,
		Flow,
		Rainfall,
		Erosion,
		Deposition,
		Evaporation,
		RockHardness,
		Weathering,
		SoilDepth,
		Thermal,
		Mountain,
		Foothill,
		Plains,
		Concavity,
		Convexity
	};

	struct FTerrainInspectorVisualisation
	{
		ETerrainInspectorPalette Palette = ETerrainInspectorPalette::Grayscale;
		const TCHAR* Name = TEXT("Grayscale");
		const TCHAR* Legend = TEXT("Low -> High");
	};

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

	FLinearColor TerrainInspectorLerpColor(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, T),
			FMath::Lerp(A.G, B.G, T),
			FMath::Lerp(A.B, B.B, T),
			1.0f);
	}

	FLinearColor TerrainInspectorThreeStop(float T, const FLinearColor& A, const FLinearColor& B, const FLinearColor& C)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T < 0.5f
			? TerrainInspectorLerpColor(A, B, T * 2.0f)
			: TerrainInspectorLerpColor(B, C, (T - 0.5f) * 2.0f);
	}

	FLinearColor TerrainInspectorFourStop(float T, const FLinearColor& A, const FLinearColor& B, const FLinearColor& C, const FLinearColor& D)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		if (T < 1.0f / 3.0f) return TerrainInspectorLerpColor(A, B, T * 3.0f);
		if (T < 2.0f / 3.0f) return TerrainInspectorLerpColor(B, C, (T - 1.0f / 3.0f) * 3.0f);
		return TerrainInspectorLerpColor(C, D, (T - 2.0f / 3.0f) * 3.0f);
	}

	FTerrainInspectorVisualisation TerrainInspectorGetVisualisation(FName FieldName)
	{
		if (FieldName == GaeaTerrainFieldNames::Height || FieldName == GaeaTerrainFieldNames::Elevation)
			return { ETerrainInspectorPalette::Height, TEXT("Hypsometric"), TEXT("Deep water -> shallow water -> sea level 0 -> lowland -> highland -> peak") };
		if (FieldName == GaeaTerrainFieldNames::SlopeDegrees)
			return { ETerrainInspectorPalette::Slope, TEXT("Slope"), TEXT("Flat -> rolling -> steep -> near-vertical") };
		if (FieldName == GaeaTerrainFieldNames::Flow)
			return { ETerrainInspectorPalette::Flow, TEXT("Drainage Flow"), TEXT("Low flow -> channel -> major drainage") };
		if (FieldName == GaeaTerrainFieldNames::Rainfall)
			return { ETerrainInspectorPalette::Rainfall, TEXT("Rainfall"), TEXT("Dry -> moderate precipitation -> high precipitation") };
		if (FieldName == GaeaTerrainFieldNames::HydraulicErosion || FieldName == GaeaTerrainFieldNames::Wear)
			return { ETerrainInspectorPalette::Erosion, TEXT("Erosion"), TEXT("Stable -> active erosion -> severe incision/wear") };
		if (FieldName == GaeaTerrainFieldNames::Deposition || FieldName == GaeaTerrainFieldNames::Deposits)
			return { ETerrainInspectorPalette::Deposition, TEXT("Deposition"), TEXT("No deposition -> accumulating sediment -> major deposit") };
		if (FieldName == GaeaTerrainFieldNames::Evaporation)
			return { ETerrainInspectorPalette::Evaporation, TEXT("Evaporation"), TEXT("Low evaporation -> drying -> high evaporation") };
		if (FieldName == GaeaTerrainFieldNames::RockHardness)
			return { ETerrainInspectorPalette::RockHardness, TEXT("Rock Hardness"), TEXT("Soft material -> competent rock -> very hard rock") };
		if (FieldName == GaeaTerrainFieldNames::Weathering)
			return { ETerrainInspectorPalette::Weathering, TEXT("Weathering"), TEXT("Fresh rock -> weathered -> heavily weathered") };
		if (FieldName == GaeaTerrainFieldNames::SoilDepth)
			return { ETerrainInspectorPalette::SoilDepth, TEXT("Soil Depth"), TEXT("Exposed substrate -> shallow soil -> deep soil") };
		if (FieldName == GaeaTerrainFieldNames::Thermal)
			return { ETerrainInspectorPalette::Thermal, TEXT("Thermal Process"), TEXT("Low thermal activity -> active -> high activity") };
		if (FieldName == GaeaTerrainFieldNames::Mountain)
			return { ETerrainInspectorPalette::Mountain, TEXT("Mountain Region"), TEXT("Outside region -> mountain influence") };
		if (FieldName == GaeaTerrainFieldNames::Foothill)
			return { ETerrainInspectorPalette::Foothill, TEXT("Foothill Region"), TEXT("Outside region -> foothill influence") };
		if (FieldName == GaeaTerrainFieldNames::Plains)
			return { ETerrainInspectorPalette::Plains, TEXT("Plains Region"), TEXT("Outside region -> plains influence") };
		if (FieldName == GaeaTerrainFieldNames::Concavity)
			return { ETerrainInspectorPalette::Concavity, TEXT("Concavity"), TEXT("Low concavity -> strongly concave terrain") };
		if (FieldName == GaeaTerrainFieldNames::Convexity)
			return { ETerrainInspectorPalette::Convexity, TEXT("Convexity"), TEXT("Low convexity -> strongly convex terrain") };
		return {};
	}

	FLinearColor TerrainInspectorColorForValue(const FTerrainInspectorVisualisation& Visualisation, float Value, float Minimum, float Maximum)
	{
		const float Range = FMath::Max(Maximum - Minimum, UE_SMALL_NUMBER);
		float T = FMath::Clamp((Value - Minimum) / Range, 0.0f, 1.0f);

		switch (Visualisation.Palette)
		{
		case ETerrainInspectorPalette::Height:
			if (Minimum < 0.0f && Maximum > 0.0f)
			{
				if (Value < 0.0f)
				{
					const float WaterT = FMath::Clamp((Value - Minimum) / FMath::Max(-Minimum, UE_SMALL_NUMBER), 0.0f, 1.0f);
					return TerrainInspectorThreeStop(WaterT, FLinearColor(0.005f, 0.015f, 0.08f), FLinearColor(0.015f, 0.16f, 0.42f), FLinearColor(0.05f, 0.55f, 0.78f));
				}
				const float LandT = FMath::Clamp(Value / FMath::Max(Maximum, UE_SMALL_NUMBER), 0.0f, 1.0f);
				if (LandT < 0.12f)
					return TerrainInspectorLerpColor(FLinearColor(0.76f, 0.70f, 0.46f), FLinearColor(0.18f, 0.46f, 0.18f), LandT / 0.12f);
				return TerrainInspectorFourStop((LandT - 0.12f) / 0.88f, FLinearColor(0.18f, 0.46f, 0.18f), FLinearColor(0.42f, 0.36f, 0.18f), FLinearColor(0.42f, 0.40f, 0.38f), FLinearColor(0.95f, 0.97f, 1.0f));
			}
			return TerrainInspectorFourStop(T, FLinearColor(0.12f, 0.30f, 0.12f), FLinearColor(0.42f, 0.42f, 0.18f), FLinearColor(0.42f, 0.38f, 0.34f), FLinearColor(0.96f, 0.98f, 1.0f));
		case ETerrainInspectorPalette::Slope:
			return TerrainInspectorFourStop(T, FLinearColor(0.05f, 0.25f, 0.08f), FLinearColor(0.75f, 0.72f, 0.08f), FLinearColor(0.92f, 0.28f, 0.03f), FLinearColor(0.98f, 0.95f, 0.90f));
		case ETerrainInspectorPalette::Flow:
			T = FMath::Pow(T, 0.35f);
			return TerrainInspectorFourStop(T, FLinearColor(0.005f, 0.008f, 0.025f), FLinearColor(0.015f, 0.12f, 0.42f), FLinearColor(0.02f, 0.70f, 0.92f), FLinearColor(0.92f, 1.0f, 1.0f));
		case ETerrainInspectorPalette::Rainfall:
			return TerrainInspectorFourStop(T, FLinearColor(0.35f, 0.20f, 0.06f), FLinearColor(0.62f, 0.55f, 0.18f), FLinearColor(0.08f, 0.48f, 0.34f), FLinearColor(0.08f, 0.42f, 0.90f));
		case ETerrainInspectorPalette::Erosion:
			T = FMath::Pow(T, 0.65f);
			return TerrainInspectorFourStop(T, FLinearColor(0.025f, 0.025f, 0.035f), FLinearColor(0.55f, 0.42f, 0.03f), FLinearColor(0.92f, 0.28f, 0.015f), FLinearColor(1.0f, 0.92f, 0.32f));
		case ETerrainInspectorPalette::Deposition:
			T = FMath::Pow(T, 0.65f);
			return TerrainInspectorFourStop(T, FLinearColor(0.025f, 0.02f, 0.015f), FLinearColor(0.28f, 0.14f, 0.045f), FLinearColor(0.72f, 0.48f, 0.12f), FLinearColor(0.98f, 0.88f, 0.55f));
		case ETerrainInspectorPalette::Evaporation:
			return TerrainInspectorFourStop(T, FLinearColor(0.06f, 0.03f, 0.16f), FLinearColor(0.42f, 0.08f, 0.34f), FLinearColor(0.94f, 0.26f, 0.03f), FLinearColor(1.0f, 0.88f, 0.18f));
		case ETerrainInspectorPalette::RockHardness:
			return TerrainInspectorFourStop(T, FLinearColor(0.24f, 0.10f, 0.04f), FLinearColor(0.46f, 0.32f, 0.20f), FLinearColor(0.42f, 0.44f, 0.47f), FLinearColor(0.90f, 0.92f, 0.94f));
		case ETerrainInspectorPalette::Weathering:
			return TerrainInspectorFourStop(T, FLinearColor(0.10f, 0.07f, 0.05f), FLinearColor(0.38f, 0.18f, 0.06f), FLinearColor(0.72f, 0.38f, 0.10f), FLinearColor(0.92f, 0.75f, 0.48f));
		case ETerrainInspectorPalette::SoilDepth:
			return TerrainInspectorFourStop(T, FLinearColor(0.035f, 0.025f, 0.02f), FLinearColor(0.18f, 0.08f, 0.025f), FLinearColor(0.42f, 0.22f, 0.08f), FLinearColor(0.68f, 0.50f, 0.28f));
		case ETerrainInspectorPalette::Thermal:
			return TerrainInspectorFourStop(T, FLinearColor(0.04f, 0.025f, 0.12f), FLinearColor(0.35f, 0.04f, 0.50f), FLinearColor(0.90f, 0.12f, 0.28f), FLinearColor(1.0f, 0.72f, 0.12f));
		case ETerrainInspectorPalette::Mountain:
			return TerrainInspectorLerpColor(FLinearColor(0.01f, 0.01f, 0.012f), FLinearColor(0.94f, 0.94f, 0.98f), T);
		case ETerrainInspectorPalette::Foothill:
			return TerrainInspectorLerpColor(FLinearColor(0.01f, 0.01f, 0.012f), FLinearColor(0.76f, 0.48f, 0.12f), T);
		case ETerrainInspectorPalette::Plains:
			return TerrainInspectorLerpColor(FLinearColor(0.01f, 0.01f, 0.012f), FLinearColor(0.28f, 0.72f, 0.20f), T);
		case ETerrainInspectorPalette::Concavity:
			return TerrainInspectorThreeStop(T, FLinearColor(0.01f, 0.01f, 0.015f), FLinearColor(0.02f, 0.20f, 0.34f), FLinearColor(0.04f, 0.92f, 0.94f));
		case ETerrainInspectorPalette::Convexity:
			return TerrainInspectorThreeStop(T, FLinearColor(0.01f, 0.01f, 0.015f), FLinearColor(0.32f, 0.05f, 0.32f), FLinearColor(0.96f, 0.20f, 0.78f));
		case ETerrainInspectorPalette::Grayscale:
		default:
			return FLinearColor(T, T, T, 1.0f);
		}
	}
}

void SGaeaTerrainInspector::Construct(const FArguments& InArgs)
{
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.ImageSize = FVector2D(TerrainInspectorPreviewResolution, TerrainInspectorPreviewResolution);
	const TSharedRef<SWidget> OutputPanel = InArgs._OutputPanel.IsValid()
		? InArgs._OutputPanel.ToSharedRef()
		: SNullWidget::NullWidget;

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
				.Value(0.18f)
				[
					SNew(SBorder)
					.Padding(6.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Analysis Fields")))
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(FieldListView, SListView<TSharedPtr<FName>>)
							.ListItemsSource(&FieldItems)
							.OnGenerateRow(this, &SGaeaTerrainInspector::GenerateFieldRow)
							.OnSelectionChanged(this, &SGaeaTerrainInspector::OnFieldSelectionChanged)
						]
					]
				]

				+ SSplitter::Slot()
				.Value(0.32f)
				[
					SNew(SBorder)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(this, &SGaeaTerrainInspector::GetFieldMetadataText)
						]
						+ SVerticalBox::Slot().FillHeight(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.Padding(4.0f)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
							[
								SNew(SBox)
								.WidthOverride(512.0f)
								.HeightOverride(512.0f)
								[
									SAssignNew(PreviewImage, SImage).Image(&PreviewBrush)
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(this, &SGaeaTerrainInspector::GetPreviewStatsText)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(this, &SGaeaTerrainInspector::GetEmptyStateText)
						]
					]
				]

				+ SSplitter::Slot()
				.Value(0.28f)
				[
					SNew(SBorder)
					.Padding(6.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Mesh Preview")))
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(MeshPreview, SGaeaTerrainMeshPreview)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text_Lambda([this]() { return MeshPreview.IsValid() ? MeshPreview->GetStatusText() : FText::GetEmpty(); })
						]
					]
				]

				+ SSplitter::Slot()
				.Value(0.22f)
				[
					OutputPanel
				]
			]
		]
	];

	RefreshFromRegistry();
}

void SGaeaTerrainInspector::RefreshFromRegistry()
{
	FGaeaTerrainDatasetSnapshot NewSnapshot;
	if (FGaeaTerrainDatasetRegistry::GetLatest(NewSnapshot)) Snapshot = MoveTemp(NewSnapshot);
	else Snapshot = FGaeaTerrainDatasetSnapshot();

	FieldItems.Reset();
	SelectedFieldName = NAME_None;

	if (Snapshot.IsValid())
	{
		TArray<FName> Names;
		Snapshot.Dataset.GetScalarFieldNames(Names);
		Names.Sort(FNameLexicalLess());
		for (const FName Name : Names) FieldItems.Add(MakeShared<FName>(Name));
	}

	if (FieldListView.IsValid())
	{
		FieldListView->RequestListRefresh();
		if (!FieldItems.IsEmpty()) FieldListView->SetSelection(FieldItems[0]);
	}

	if (MeshPreview.IsValid())
	{
		if (Snapshot.IsValid()) MeshPreview->SetTerrain(Snapshot);
		else MeshPreview->ClearTerrain();
	}

	RebuildPreview();
}

FReply SGaeaTerrainInspector::RefreshDataset()
{
	RefreshFromRegistry();
	return FReply::Handled();
}

TSharedRef<ITableRow> SGaeaTerrainInspector::GenerateFieldRow(TSharedPtr<FName> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
	[
		SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? *Item : NAME_None))
	];
}

void SGaeaTerrainInspector::OnFieldSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	SelectedFieldName = Item.IsValid() ? *Item : NAME_None;
	RebuildPreview();
}

const FGaeaScalarField* SGaeaTerrainInspector::GetSelectedField() const
{
	if (!Snapshot.IsValid() || SelectedFieldName.IsNone()) return nullptr;
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
	if (PreviewImage.IsValid()) PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
}

void SGaeaTerrainInspector::RebuildPreview()
{
	ClearPreview();
	const FGaeaScalarField* Field = GetSelectedField();
	if (!Field || !Field->IsValid()) return;

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

	if (PreviewSampleCount <= 0) { ClearPreview(); return; }
	PreviewMeanValue = static_cast<float>(Sum / static_cast<double>(PreviewSampleCount));
	const double Variance = FMath::Max(0.0, SumSquares / static_cast<double>(PreviewSampleCount) - static_cast<double>(PreviewMeanValue) * static_cast<double>(PreviewMeanValue));
	PreviewStdDev = static_cast<float>(FMath::Sqrt(Variance));

	const FTerrainInspectorVisualisation Visualisation = TerrainInspectorGetVisualisation(SelectedFieldName);
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TerrainInspectorPreviewResolution * TerrainInspectorPreviewResolution);

	for (int32 PreviewY = 0; PreviewY < TerrainInspectorPreviewResolution; ++PreviewY)
	{
		const double SourceY = static_cast<double>(PreviewY) / static_cast<double>(TerrainInspectorPreviewResolution - 1) * static_cast<double>(Dimensions.Y - 1);
		for (int32 PreviewX = 0; PreviewX < TerrainInspectorPreviewResolution; ++PreviewX)
		{
			const double SourceX = static_cast<double>(PreviewX) / static_cast<double>(TerrainInspectorPreviewResolution - 1) * static_cast<double>(Dimensions.X - 1);
			const float Value = TerrainInspectorSampleBilinear(*Field, SourceX, SourceY);
			const FLinearColor Color = TerrainInspectorColorForValue(Visualisation, Value, PreviewMinValue, PreviewMaxValue);
			Pixels[PreviewY * TerrainInspectorPreviewResolution + PreviewX] = Color.ToFColor(true);
		}
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(TerrainInspectorPreviewResolution, TerrainInspectorPreviewResolution, PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty()) { ClearPreview(); return; }

	Texture->SRGB = true;
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
	if (PreviewImage.IsValid()) PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
}

FText SGaeaTerrainInspector::GetSourceText() const
{
	if (!Snapshot.IsValid()) return FText::FromString(TEXT("No terrain dataset has been published yet."));
	return FText::FromString(FString::Printf(TEXT("Source: %s   Revision: %llu   Fields: %d   Height scale: %.1f"), *Snapshot.SourceId.ToString(), static_cast<unsigned long long>(Snapshot.Revision), Snapshot.Dataset.NumScalarFields(), Snapshot.Metadata.HeightScale));
}

FText SGaeaTerrainInspector::GetFieldMetadataText() const
{
	const FGaeaScalarField* Field = GetSelectedField();
	if (!Field) return FText::FromString(TEXT("Select a terrain field to inspect it."));

	const FGaeaGridDomain& Domain = Field->Domain;
	const FVector2d CellSize = Domain.GetCellSize();
	const FTerrainInspectorVisualisation Visualisation = TerrainInspectorGetVisualisation(SelectedFieldName);
	return FText::FromString(FString::Printf(
		TEXT("%s\nUnit: %s   Interpolation: %s\nResolution: %d x %d   Border: %d\nWorld: [%.1f, %.1f] to [%.1f, %.1f]\nCell size: %.2f x %.2f\nVisualisation: %s\nLegend: %s\nPreview: %d x %d, bilinear"),
		*Field->Descriptor.Name.ToString(), *TerrainInspectorFieldUnitToString(Field->Descriptor.Unit), *TerrainInspectorInterpolationToString(Field->Descriptor.Interpolation), Domain.Dimensions.X, Domain.Dimensions.Y, Domain.BorderSamples, Domain.WorldMin.X, Domain.WorldMin.Y, Domain.WorldMax.X, Domain.WorldMax.Y, CellSize.X, CellSize.Y, Visualisation.Name, Visualisation.Legend, TerrainInspectorPreviewResolution, TerrainInspectorPreviewResolution));
}

FText SGaeaTerrainInspector::GetPreviewStatsText() const
{
	if (PreviewSampleCount <= 0) return FText::GetEmpty();
	return FText::FromString(FString::Printf(TEXT("Min %.6g   Max %.6g   Mean %.6g   StdDev %.6g   Samples %lld"), PreviewMinValue, PreviewMaxValue, PreviewMeanValue, PreviewStdDev, static_cast<long long>(PreviewSampleCount)));
}

FText SGaeaTerrainInspector::GetEmptyStateText() const
{
	if (!Snapshot.IsValid()) return FText::FromString(TEXT("Connect a terrain result to inspect and preview it automatically."));
	if (!GetSelectedField()) return FText::FromString(TEXT("Select a terrain field to inspect it."));
	return FText::GetEmpty();
}
