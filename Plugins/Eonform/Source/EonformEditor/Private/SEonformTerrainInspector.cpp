#include "SEonformTerrainInspector.h"

#include "Engine/Texture2D.h"
#include "EonformScalarField.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "SEonformTerrainGraphPanel.h"
#include "SEonformTerrainMeshPreview.h"
#include "SEonformTerrainRegionGrid.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
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
		FlowDirection,
		StreamOrder,
		Distance,
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
		bool bLogarithmic = false;
		bool bCategorical = false;
	};

	FString TerrainInspectorFieldUnitToString(EEonformFieldUnit Unit)
	{
		switch (Unit)
		{
		case EEonformFieldUnit::Unitless: return TEXT("Unitless");
		case EEonformFieldUnit::Normalized: return TEXT("Normalized");
		case EEonformFieldUnit::Centimeters: return TEXT("Centimeters");
		case EEonformFieldUnit::Meters: return TEXT("Meters");
		case EEonformFieldUnit::Kilometers: return TEXT("Kilometers");
		case EEonformFieldUnit::SquareKilometers: return TEXT("Square Kilometers");
		case EEonformFieldUnit::Degrees: return TEXT("Degrees");
		case EEonformFieldUnit::Celsius: return TEXT("Celsius");
		default: return TEXT("Unknown");
		}
	}

	FString TerrainInspectorInterpolationToString(EEonformInterpolation Interpolation)
	{
		switch (Interpolation)
		{
		case EEonformInterpolation::Nearest: return TEXT("Nearest");
		case EEonformInterpolation::Bilinear: return TEXT("Bilinear");
		default: return TEXT("Unknown");
		}
	}

	float TerrainInspectorSampleNearest(const FEonformScalarField& Field, double X, double Y)
	{
		const int32 Width = Field.Domain.Dimensions.X;
		const int32 Height = Field.Domain.Dimensions.Y;
		const int32 IX = FMath::Clamp(FMath::RoundToInt(X), 0, Width - 1);
		const int32 IY = FMath::Clamp(FMath::RoundToInt(Y), 0, Height - 1);
		return Field.AtInterior(IX, IY);
	}

	float TerrainInspectorSampleBilinear(const FEonformScalarField& Field, double X, double Y)
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
		if (FieldName == EonformTerrainFieldNames::Height || FieldName == EonformTerrainFieldNames::Elevation)
			return { ETerrainInspectorPalette::Height, TEXT("Hypsometric"), TEXT("Deep water -> shallow water -> sea level 0 -> lowland -> highland -> peak") };
		if (FieldName == EonformTerrainFieldNames::SlopeDegrees)
			return { ETerrainInspectorPalette::Slope, TEXT("Slope"), TEXT("Flat -> rolling -> steep -> near-vertical") };
		if (FieldName == EonformTerrainFieldNames::Flow)
			return { ETerrainInspectorPalette::Flow, TEXT("Drainage Flow"), TEXT("Low flow -> channel -> major drainage") };
		if (FieldName == EonformTerrainFieldNames::FlowAccumulation)
			return { ETerrainInspectorPalette::Flow, TEXT("Flow Accumulation (log)"), TEXT("Headwater -> tributary -> trunk channel"), true, false };
		if (FieldName == EonformTerrainFieldNames::CatchmentAreaKm2)
			return { ETerrainInspectorPalette::Flow, TEXT("Catchment Area (log)"), TEXT("Small contributing area -> major catchment"), true, false };
		if (FieldName == EonformTerrainFieldNames::FlowDirection)
			return { ETerrainInspectorPalette::FlowDirection, TEXT("D8 Flow Direction"), TEXT("Outlet + eight discrete D8 directions"), false, true };
		if (FieldName == EonformTerrainFieldNames::StreamOrder)
			return { ETerrainInspectorPalette::StreamOrder, TEXT("Strahler Stream Order"), TEXT("Order 1 headwater -> higher-order trunk"), false, true };
		if (FieldName == EonformTerrainFieldNames::DistanceToOutletKm)
			return { ETerrainInspectorPalette::Distance, TEXT("Distance To Outlet"), TEXT("Outlet -> increasingly distant headwater") };
		if (FieldName == EonformTerrainFieldNames::Rainfall)
			return { ETerrainInspectorPalette::Rainfall, TEXT("Rainfall"), TEXT("Dry -> moderate precipitation -> high precipitation") };
		if (FieldName == EonformTerrainFieldNames::HydraulicErosion || FieldName == EonformTerrainFieldNames::Wear)
			return { ETerrainInspectorPalette::Erosion, TEXT("Erosion"), TEXT("Stable -> active erosion -> severe incision/wear") };
		if (FieldName == EonformTerrainFieldNames::Deposition || FieldName == EonformTerrainFieldNames::Deposits)
			return { ETerrainInspectorPalette::Deposition, TEXT("Deposition"), TEXT("No deposition -> accumulating sediment -> major deposit") };
		if (FieldName == EonformTerrainFieldNames::Evaporation)
			return { ETerrainInspectorPalette::Evaporation, TEXT("Evaporation"), TEXT("Low evaporation -> drying -> high evaporation") };
		if (FieldName == EonformTerrainFieldNames::RockHardness)
			return { ETerrainInspectorPalette::RockHardness, TEXT("Rock Hardness"), TEXT("Soft material -> competent rock -> very hard rock") };
		if (FieldName == EonformTerrainFieldNames::Weathering)
			return { ETerrainInspectorPalette::Weathering, TEXT("Weathering"), TEXT("Fresh rock -> weathered -> heavily weathered") };
		if (FieldName == EonformTerrainFieldNames::SoilDepth)
			return { ETerrainInspectorPalette::SoilDepth, TEXT("Soil Depth"), TEXT("Exposed substrate -> shallow soil -> deep soil") };
		if (FieldName == EonformTerrainFieldNames::Thermal)
			return { ETerrainInspectorPalette::Thermal, TEXT("Thermal Process"), TEXT("Low thermal activity -> active -> high activity") };
		if (FieldName == EonformTerrainFieldNames::Mountain)
			return { ETerrainInspectorPalette::Mountain, TEXT("Mountain Region"), TEXT("Outside region -> mountain influence") };
		if (FieldName == EonformTerrainFieldNames::Foothill)
			return { ETerrainInspectorPalette::Foothill, TEXT("Foothill Region"), TEXT("Outside region -> foothill influence") };
		if (FieldName == EonformTerrainFieldNames::Plains)
			return { ETerrainInspectorPalette::Plains, TEXT("Plains Region"), TEXT("Outside region -> plains influence") };
		if (FieldName == EonformTerrainFieldNames::Concavity)
			return { ETerrainInspectorPalette::Concavity, TEXT("Concavity"), TEXT("Low concavity -> strongly concave terrain") };
		if (FieldName == EonformTerrainFieldNames::Convexity)
			return { ETerrainInspectorPalette::Convexity, TEXT("Convexity"), TEXT("Low convexity -> strongly convex terrain") };
		return {};
	}

	FLinearColor TerrainInspectorDirectionColor(float Value)
	{
		const int32 Direction = FMath::RoundToInt(Value);
		if (Direction < 0) return FLinearColor(0.025f, 0.025f, 0.03f);
		static const FLinearColor Colors[8] =
		{
			FLinearColor(0.92f, 0.20f, 0.18f),
			FLinearColor(0.98f, 0.56f, 0.12f),
			FLinearColor(0.90f, 0.86f, 0.16f),
			FLinearColor(0.28f, 0.78f, 0.22f),
			FLinearColor(0.12f, 0.72f, 0.72f),
			FLinearColor(0.14f, 0.38f, 0.90f),
			FLinearColor(0.50f, 0.22f, 0.88f),
			FLinearColor(0.88f, 0.22f, 0.68f)
		};
		return Colors[FMath::Clamp(Direction, 0, 7)];
	}

	FLinearColor TerrainInspectorColorForValue(const FTerrainInspectorVisualisation& Visualisation, float Value, float Minimum, float Maximum)
	{
		if (!FMath::IsFinite(Value)) return FLinearColor(1.0f, 0.0f, 1.0f);
		if (Visualisation.Palette == ETerrainInspectorPalette::FlowDirection) return TerrainInspectorDirectionColor(Value);

		const float Range = FMath::Max(Maximum - Minimum, UE_SMALL_NUMBER);
		float T = FMath::Clamp((Value - Minimum) / Range, 0.0f, 1.0f);
		if (Visualisation.bLogarithmic)
		{
			const double Shifted = FMath::Max(static_cast<double>(Value - Minimum), 0.0);
			const double LogRange = FMath::Loge(1.0 + static_cast<double>(Range));
			T = LogRange > UE_DOUBLE_SMALL_NUMBER
				? static_cast<float>(FMath::Loge(1.0 + Shifted) / LogRange)
				: 0.0f;
		}

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
			return TerrainInspectorFourStop(T, FLinearColor(0.05f, 0.25f, 0.08f), FLinearColor(0.75f, 0.72f, 0.08f), FLinearColor(0.92f, 0.28f, 0.015f), FLinearColor(0.98f, 0.95f, 0.90f));
		case ETerrainInspectorPalette::Flow:
			if (!Visualisation.bLogarithmic) T = FMath::Pow(T, 0.35f);
			return TerrainInspectorFourStop(T, FLinearColor(0.005f, 0.008f, 0.025f), FLinearColor(0.015f, 0.12f, 0.42f), FLinearColor(0.02f, 0.70f, 0.92f), FLinearColor(0.92f, 1.0f, 1.0f));
		case ETerrainInspectorPalette::StreamOrder:
		{
			const int32 Order = FMath::Max(1, FMath::RoundToInt(Value));
			const float OrderT = FMath::Clamp(static_cast<float>(Order - 1) / FMath::Max(Maximum - 1.0f, 1.0f), 0.0f, 1.0f);
			return TerrainInspectorFourStop(OrderT, FLinearColor(0.12f, 0.34f, 0.78f), FLinearColor(0.08f, 0.72f, 0.78f), FLinearColor(0.92f, 0.72f, 0.12f), FLinearColor(0.96f, 0.18f, 0.10f));
		}
		case ETerrainInspectorPalette::Distance:
			return TerrainInspectorFourStop(T, FLinearColor(0.02f, 0.03f, 0.10f), FLinearColor(0.08f, 0.28f, 0.72f), FLinearColor(0.16f, 0.78f, 0.70f), FLinearColor(0.96f, 0.90f, 0.36f));
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
		case ETerrainInspectorPalette::FlowDirection:
		case ETerrainInspectorPalette::Grayscale:
		default:
			return FLinearColor(T, T, T, 1.0f);
		}
	}
}

void SEonformTerrainInspector::Construct(const FArguments& InArgs)
{
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.ImageSize = FVector2D(TerrainInspectorPreviewResolution, TerrainInspectorPreviewResolution);
	const TSharedRef<SWidget> OutputPanel = InArgs._OutputPanel.IsValid()
		? InArgs._OutputPanel.ToSharedRef()
		: SNullWidget::NullWidget;

	TSharedPtr<SVerticalBox> NodeDetailsPanel;
	const TSharedRef<SWidget> NodeDetails =
		SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Node Details")))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(NodeDetailsPanel, SVerticalBox)
				]
			]
		];

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
					SNew(STextBlock).Text(this, &SEonformTerrainInspector::GetSourceText)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Refresh")))
					.OnClicked(this, &SEonformTerrainInspector::RefreshDataset)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)

				+ SSplitter::Slot().Value(0.76f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.FillHeight(0.62f)
					.Padding(0.0f, 0.0f, 8.0f, 8.0f)
					[
						SNew(SEonformTerrainGraphPanel)
						.OnEvaluated(FSimpleDelegate::CreateSP(this, &SEonformTerrainInspector::RefreshFromRegistry))
						.ParameterPanel(NodeDetailsPanel)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 8.0f, 8.0f)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.Padding(4.0f)
						.HeaderContent()
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Analysis Fields")))
						]
						.BodyContent()
						[
							SNew(SBox)
							.HeightOverride(300.0f)
							[
								SNew(SSplitter)
								+ SSplitter::Slot().Value(0.28f)
								[
									SNew(SBorder)
									.Padding(6.0f)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
									[
										SAssignNew(FieldListView, SListView<TSharedPtr<FName>>)
										.ListItemsSource(&FieldItems)
										.OnGenerateRow(this, &SEonformTerrainInspector::GenerateFieldRow)
										.OnSelectionChanged(this, &SEonformTerrainInspector::OnFieldSelectionChanged)
									]
								]
								+ SSplitter::Slot().Value(0.72f)
								[
									SNew(SBorder)
									.Padding(8.0f)
									.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
										[
											SNew(SBorder)
											.Padding(4.0f)
											.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
											[
												SNew(SBox).WidthOverride(256.0f).HeightOverride(256.0f)
												[
													SAssignNew(PreviewImage, SImage).Image(&PreviewBrush)
												]
											]
										]
										+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(10.0f, 0.0f, 0.0f, 0.0f)
										[
											SNew(SVerticalBox)
											+ SVerticalBox::Slot().AutoHeight()
											[
												SNew(STextBlock).AutoWrapText(true).Text(this, &SEonformTerrainInspector::GetFieldMetadataText)
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
											[
												SNew(STextBlock).AutoWrapText(true).Text(this, &SEonformTerrainInspector::GetPreviewStatsText)
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
											[
												SNew(STextBlock).AutoWrapText(true).Text(this, &SEonformTerrainInspector::GetEmptyStateText)
											]
										]
									]
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.FillHeight(0.38f)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SSplitter)
						+ SSplitter::Slot().Value(0.48f)
						[
							SNew(SBorder)
							.Padding(6.0f)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("Terrain Preview")))
								]
								+ SVerticalBox::Slot().FillHeight(1.0f)
								[
									SAssignNew(MeshPreview, SEonformTerrainMeshPreview)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
								[
									SNew(STextBlock)
									.AutoWrapText(true)
									.Text_Lambda([this]() { return MeshPreview.IsValid() ? MeshPreview->GetStatusText() : FText::GetEmpty(); })
								]
							]
						]
						+ SSplitter::Slot().Value(0.52f)
						[
							SNew(SBorder)
							.Padding(8.0f)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							[
								SNew(SEonformTerrainRegionGrid).SourceId(TEXT("EonformGraph"))
							]
						]
					]
				]

				+ SSplitter::Slot().Value(0.24f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						NodeDetails
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						OutputPanel
					]
				]
			]
		]
	];

	RefreshFromRegistry();
}

void SEonformTerrainInspector::RefreshFromRegistry()
{
	FEonformTerrainDatasetSnapshot NewSnapshot;
	if (FEonformTerrainDatasetRegistry::Get(TEXT("EonformGraph"), NewSnapshot) && NewSnapshot.IsValid())
	{
		Snapshot = MoveTemp(NewSnapshot);
	}
	else
	{
		Snapshot = FEonformTerrainDatasetSnapshot();
	}

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

FReply SEonformTerrainInspector::RefreshDataset()
{
	RefreshFromRegistry();
	return FReply::Handled();
}

TSharedRef<ITableRow> SEonformTerrainInspector::GenerateFieldRow(TSharedPtr<FName> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
	[
		SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? *Item : NAME_None))
	];
}

void SEonformTerrainInspector::OnFieldSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	SelectedFieldName = Item.IsValid() ? *Item : NAME_None;
	RebuildPreview();
}

const FEonformScalarField* SEonformTerrainInspector::GetSelectedField() const
{
	if (!Snapshot.IsValid() || SelectedFieldName.IsNone()) return nullptr;
	return Snapshot.Dataset.FindScalarField(SelectedFieldName);
}

void SEonformTerrainInspector::ClearPreview()
{
	PreviewTexture.Reset();
	PreviewBrush.SetResourceObject(nullptr);
	PreviewMinValue = 0.0f;
	PreviewMaxValue = 0.0f;
	PreviewMeanValue = 0.0f;
	PreviewStdDev = 0.0f;
	PreviewSampleCount = 0;
	PreviewNonFiniteCount = 0;
	if (PreviewImage.IsValid()) PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
}

void SEonformTerrainInspector::RebuildPreview()
{
	ClearPreview();
	const FEonformScalarField* Field = GetSelectedField();
	if (!Field || !Field->IsValid()) return;

	const FIntPoint Dimensions = Field->Domain.Dimensions;
	const FTerrainInspectorVisualisation Visualisation = TerrainInspectorGetVisualisation(SelectedFieldName);
	const bool bUseNearest = Visualisation.bCategorical || Field->Descriptor.Interpolation == EEonformInterpolation::Nearest;

	PreviewMinValue = TNumericLimits<float>::Max();
	PreviewMaxValue = TNumericLimits<float>::Lowest();
	double Sum = 0.0;
	double SumSquares = 0.0;
	PreviewSampleCount = 0;

	TArray<float> SampledValues;
	SampledValues.SetNumUninitialized(TerrainInspectorPreviewResolution * TerrainInspectorPreviewResolution);
	for (int32 PreviewY = 0; PreviewY < TerrainInspectorPreviewResolution; ++PreviewY)
	{
		const double SourceY = static_cast<double>(PreviewY) / static_cast<double>(TerrainInspectorPreviewResolution - 1)
			* static_cast<double>(Dimensions.Y - 1);
		for (int32 PreviewX = 0; PreviewX < TerrainInspectorPreviewResolution; ++PreviewX)
		{
			const double SourceX = static_cast<double>(PreviewX) / static_cast<double>(TerrainInspectorPreviewResolution - 1)
				* static_cast<double>(Dimensions.X - 1);
			const float Value = bUseNearest
				? TerrainInspectorSampleNearest(*Field, SourceX, SourceY)
				: TerrainInspectorSampleBilinear(*Field, SourceX, SourceY);
			SampledValues[PreviewY * TerrainInspectorPreviewResolution + PreviewX] = Value;

			if (!FMath::IsFinite(Value))
			{
				++PreviewNonFiniteCount;
				continue;
			}
			PreviewMinValue = FMath::Min(PreviewMinValue, Value);
			PreviewMaxValue = FMath::Max(PreviewMaxValue, Value);
			Sum += static_cast<double>(Value);
			SumSquares += static_cast<double>(Value) * static_cast<double>(Value);
			++PreviewSampleCount;
		}
	}

	if (PreviewSampleCount <= 0) { ClearPreview(); return; }
	PreviewMeanValue = static_cast<float>(Sum / static_cast<double>(PreviewSampleCount));
	const double Variance = FMath::Max(
		0.0,
		SumSquares / static_cast<double>(PreviewSampleCount)
			- static_cast<double>(PreviewMeanValue) * static_cast<double>(PreviewMeanValue));
	PreviewStdDev = static_cast<float>(FMath::Sqrt(Variance));

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TerrainInspectorPreviewResolution * TerrainInspectorPreviewResolution);
	for (int32 Index = 0; Index < SampledValues.Num(); ++Index)
	{
		Pixels[Index] = TerrainInspectorColorForValue(
			Visualisation,
			SampledValues[Index],
			PreviewMinValue,
			PreviewMaxValue).ToFColor(true);
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(TerrainInspectorPreviewResolution, TerrainInspectorPreviewResolution, PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty()) { ClearPreview(); return; }

	Texture->SRGB = true;
	Texture->Filter = bUseNearest ? TF_Nearest : TF_Bilinear;
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

FText SEonformTerrainInspector::GetSourceText() const
{
	if (!Snapshot.IsValid()) return FText::FromString(TEXT("No terrain dataset has been published yet."));
	return FText::FromString(FString::Printf(TEXT("Source: %s   Revision: %llu   Fields: %d   Height scale: %.1f"), *Snapshot.SourceId.ToString(), static_cast<unsigned long long>(Snapshot.Revision), Snapshot.Dataset.NumScalarFields(), Snapshot.Metadata.HeightScale));
}

FText SEonformTerrainInspector::GetFieldMetadataText() const
{
	const FEonformScalarField* Field = GetSelectedField();
	if (!Field) return FText::FromString(TEXT("Select a terrain field to inspect it."));

	const FEonformGridDomain& Domain = Field->Domain;
	const FVector2d AuthoredCellSize = Domain.GetCellSize();
	const FTerrainInspectorVisualisation Visualisation = TerrainInspectorGetVisualisation(SelectedFieldName);
	const FEonformTerrainPhysicalMetrics Physical = FEonformTerrainPhysicalContext::GetActive();
	const bool bUseNearest = Visualisation.bCategorical || Field->Descriptor.Interpolation == EEonformInterpolation::Nearest;
	const FString PreviewMode = Visualisation.bLogarithmic
		? FString::Printf(TEXT("%s, logarithmic display"), bUseNearest ? TEXT("nearest") : TEXT("bilinear"))
		: FString(bUseNearest ? TEXT("nearest") : TEXT("bilinear"));

	FString PhysicalText;
	if (Physical.HasWorldDimensions())
	{
		const FVector2d PhysicalSpacing = Physical.ResolveSampleSpacingMeters(Domain.Dimensions, AuthoredCellSize);
		PhysicalText = FString::Printf(
			TEXT("\nPhysical world: %.3f x %.3f km   Sample spacing: %.3f x %.3f m"),
			Physical.WorldWidthMeters / 1000.0,
			Physical.WorldDepthMeters / 1000.0,
			PhysicalSpacing.X,
			PhysicalSpacing.Y);
	}
	if (Physical.HasElevationScale())
	{
		PhysicalText += FString::Printf(
			TEXT("\nElevation scale: %.3f m   Sea level: %.3f m"),
			Physical.ElevationScaleMeters,
			Physical.SeaLevelMeters);
	}

	return FText::FromString(FString::Printf(
		TEXT("%s\nUnit: %s   Interpolation: %s\nResolution: %d x %d   Border: %d\nAuthored domain: [%.1f, %.1f] to [%.1f, %.1f] cm\nAuthored cell: %.2f x %.2f cm%s\nVisualisation: %s\nLegend: %s\nPreview: %d x %d, %s"),
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
		AuthoredCellSize.X,
		AuthoredCellSize.Y,
		*PhysicalText,
		Visualisation.Name,
		Visualisation.Legend,
		TerrainInspectorPreviewResolution,
		TerrainInspectorPreviewResolution,
		*PreviewMode));
}

FText SEonformTerrainInspector::GetPreviewStatsText() const
{
	if (PreviewSampleCount <= 0) return FText::GetEmpty();
	return FText::FromString(FString::Printf(
		TEXT("Preview-sampled: Min %.6g   Max %.6g   Mean %.6g   StdDev %.6g   Finite %lld   NonFinite %lld"),
		PreviewMinValue,
		PreviewMaxValue,
		PreviewMeanValue,
		PreviewStdDev,
		static_cast<long long>(PreviewSampleCount),
		static_cast<long long>(PreviewNonFiniteCount)));
}

FText SEonformTerrainInspector::GetEmptyStateText() const
{
	if (!Snapshot.IsValid()) return FText::FromString(TEXT("Connect or edit the graph; EONFORM will analyse it automatically."));
	if (!GetSelectedField()) return FText::FromString(TEXT("Select a terrain field to inspect it."));
	return FText::GetEmpty();
}
