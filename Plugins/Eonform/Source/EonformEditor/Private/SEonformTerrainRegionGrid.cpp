#include "SEonformTerrainRegionGrid.h"

#include "Editor.h"
#include "Selection.h"
#include "EonformMeshTerrainOutput.h"
#include "EonformTerrainDatasetRegistry.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainOutputEditorState.h"
#include "EonformTerrainRegionRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace EonformTerrainRegionGridPrivate
{
	constexpr double OutputCentimetersPerKilometer = 100000.0;
	constexpr double OutputCentimetersPerMeter = 100.0;
	constexpr float DefaultRegionCellSize = 88.0f;
	constexpr float MinRegionCellSize = 48.0f;
	constexpr float MaxRegionCellSize = 180.0f;
	constexpr float RegionZoomStep = 16.0f;

	struct FMeshTerrainPreviewLayout
	{
		bool bValid = false;
		FIntPoint Resolution = FIntPoint::ZeroValue;
		FIntPoint Regions = FIntPoint::ZeroValue;
		FEonformMeshTerrainLayoutEstimate Estimate;
	};

	FString EvaluationStateText(EEonformTerrainRegionEvaluationState State)
	{
		switch (State)
		{
		case EEonformTerrainRegionEvaluationState::Known: return TEXT("Known");
		case EEonformTerrainRegionEvaluationState::Queued: return TEXT("Queued");
		case EEonformTerrainRegionEvaluationState::Evaluating: return TEXT("Evaluating");
		case EEonformTerrainRegionEvaluationState::Evaluated: return TEXT("Evaluated");
		case EEonformTerrainRegionEvaluationState::Failed: return TEXT("Failed");
		default: return TEXT("Unknown");
		}
	}

	FString MaterializationStateText(EEonformTerrainRegionMaterializationState State)
	{
		switch (State)
		{
		case EEonformTerrainRegionMaterializationState::Unloaded: return TEXT("Unloaded");
		case EEonformTerrainRegionMaterializationState::Prepared: return TEXT("Prepared");
		case EEonformTerrainRegionMaterializationState::Committing: return TEXT("Committing");
		case EEonformTerrainRegionMaterializationState::Loaded: return TEXT("Loaded");
		case EEonformTerrainRegionMaterializationState::Evicting: return TEXT("Evicting");
		default: return TEXT("Unknown");
		}
	}

	FString RegionLabel(const FIntPoint& Coordinate)
	{
		FString Column;
		int32 Value = FMath::Max(Coordinate.X, 0);
		do
		{
			Column.InsertAt(0, TCHAR(TEXT('A') + (Value % 26)));
			Value = (Value / 26) - 1;
		}
		while (Value >= 0);
		return FString::Printf(TEXT("%s%d"), *Column, Coordinate.Y + 1);
	}

	FString RegionStateText(const FEonformTerrainRegionSnapshot* Snapshot, bool bLoaded)
	{
		if (bLoaded) return TEXT("LOADED");
		if (!Snapshot) return TEXT("UNLOADED");
		if (!Snapshot->Error.IsEmpty() || Snapshot->EvaluationState == EEonformTerrainRegionEvaluationState::Failed)
		{
			return TEXT("FAILED");
		}
		if (Snapshot->MaterializationState == EEonformTerrainRegionMaterializationState::Committing)
		{
			return TEXT("BUILDING");
		}
		if (Snapshot->MaterializationState == EEonformTerrainRegionMaterializationState::Evicting)
		{
			return TEXT("EVICTING");
		}
		if (Snapshot->EvaluationState == EEonformTerrainRegionEvaluationState::Evaluating)
		{
			return TEXT("EVALUATING");
		}
		if (Snapshot->EvaluationState == EEonformTerrainRegionEvaluationState::Queued)
		{
			return TEXT("QUEUED");
		}
		if (Snapshot->MaterializationState == EEonformTerrainRegionMaterializationState::Prepared
			|| Snapshot->EvaluationState == EEonformTerrainRegionEvaluationState::Evaluated)
		{
			return TEXT("READY");
		}
		return TEXT("UNLOADED");
	}

	bool SetsEqual(const TSet<FIntPoint>& A, const TSet<FIntPoint>& B)
	{
		if (A.Num() != B.Num()) return false;
		for (const FIntPoint& Value : A)
		{
			if (!B.Contains(Value)) return false;
		}
		return true;
	}

	uint64 GetPlanningSignature()
	{
		const FEonformTerrainOutputEditorState& State = FEonformTerrainOutputEditorState::Get();
		uint64 Signature = State.GetRevision();
		Signature ^= State.GetPublishedAnalysisRevision() + 0x9e3779b97f4a7c15ull + (Signature << 6) + (Signature >> 2);
		return Signature;
	}

	FEonformMeshTerrainOutputSettings MakeMeshTerrainSettings()
	{
		const FEonformTerrainGraphOutputSettings& GraphSettings = FEonformTerrainOutputEditorState::Get().GetSettings();
		FEonformMeshTerrainOutputSettings Settings;
		Settings.TargetResolution = GraphSettings.OutputResolution > 0
			? FIntPoint(
				FMath::Clamp(GraphSettings.OutputResolution, 17, 4097),
				FMath::Clamp(GraphSettings.OutputResolution, 17, 4097))
			: FIntPoint::ZeroValue;
		Settings.SectionLayout = GraphSettings.SectionLayout == EEonformTerrainOutputSectionLayout::Explicit
			? EEonformMeshTerrainSectionLayout::Explicit
			: EEonformMeshTerrainSectionLayout::Automatic;
		switch (GraphSettings.SectionComplexity)
		{
		case EEonformTerrainOutputComplexity::Responsive:
			Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Responsive;
			break;
		case EEonformTerrainOutputComplexity::Detailed:
			Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Detailed;
			break;
		case EEonformTerrainOutputComplexity::Maximum:
			Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Maximum;
			break;
		case EEonformTerrainOutputComplexity::Balanced:
		default:
			Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Balanced;
			break;
		}
		Settings.Sections = FIntPoint(GraphSettings.SectionsX, GraphSettings.SectionsY);
		Settings.MeshPartitionDefinition = GraphSettings.MeshPartitionDefinition.IsValid()
			? GraphSettings.MeshPartitionDefinition.TryLoad()
			: nullptr;
		return Settings;
	}

	FIntPoint ResolvePreviewResolution()
	{
		const FEonformTerrainGraphOutputSettings& GraphSettings = FEonformTerrainOutputEditorState::Get().GetSettings();
		if (GraphSettings.OutputResolution > 0)
		{
			const int32 Resolution = FMath::Clamp(GraphSettings.OutputResolution, 17, 4097);
			return FIntPoint(Resolution, Resolution);
		}

		FIntPoint NativeResolution;
		return FEonformTerrainDatasetRegistry::GetHeightResolution(TEXT("EonformGraph"), NativeResolution)
			? NativeResolution
			: FIntPoint::ZeroValue;
	}

	FMeshTerrainPreviewLayout ResolvePreviewLayout()
	{
		FMeshTerrainPreviewLayout Preview;
		Preview.Resolution = ResolvePreviewResolution();
		if (Preview.Resolution.X < 2 || Preview.Resolution.Y < 2) return Preview;

		Preview.Estimate = FEonformMeshTerrainOutput::EstimateLayout(Preview.Resolution, MakeMeshTerrainSettings());
		if (!Preview.Estimate.bValid) return Preview;

		Preview.Regions = Preview.Estimate.Sections;
		Preview.bValid = Preview.Regions.X > 0 && Preview.Regions.Y > 0;
		return Preview;
	}

	bool ResolveCurrentBuildInput(
		FEonformTerrainDatasetSnapshot& OutSnapshot,
		FEonformMeshTerrainOutputSettings& OutSettings,
		FString& OutError)
	{
		const FEonformTerrainOutputEditorState& State = FEonformTerrainOutputEditorState::Get();
		if (!State.IsAnalysisAvailable())
		{
			OutError = TEXT("No current evaluated terrain is available.");
			return false;
		}
		if (!FEonformTerrainDatasetRegistry::Get(TEXT("EonformGraph"), OutSnapshot)
			|| !OutSnapshot.IsValid()
			|| OutSnapshot.Revision != State.GetPublishedAnalysisRevision())
		{
			OutError = TEXT("The current terrain snapshot is not generation-safe.");
			return false;
		}

		const FEonformTerrainGraphOutputSettings& GraphSettings = State.GetSettings();
		OutSettings = MakeMeshTerrainSettings();

		const FEonformScalarField* Height = OutSnapshot.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			OutError = TEXT("The evaluated terrain has no valid Height field.");
			return false;
		}
		const FVector2d SourceSize = Height->Domain.WorldSize();
		const double SourceWidth = FMath::Abs(SourceSize.X);
		const double SourceDepth = FMath::Abs(SourceSize.Y);
		if (SourceWidth <= UE_DOUBLE_SMALL_NUMBER || SourceDepth <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutError = TEXT("The evaluated terrain has an invalid physical domain size.");
			return false;
		}

		const double TargetWidthCm = GraphSettings.WorldWidthKilometers * OutputCentimetersPerKilometer;
		const double TargetDepthCm = GraphSettings.WorldDepthKilometers * OutputCentimetersPerKilometer;
		const double TargetElevationCm = GraphSettings.ElevationScaleMeters * OutputCentimetersPerMeter;
		OutSettings.HorizontalScale = 1.0;
		OutSettings.HorizontalScaleXY = FVector2d(TargetWidthCm / SourceWidth, TargetDepthCm / SourceDepth);
		OutSettings.VerticalScale = TargetElevationCm
			/ FMath::Max(static_cast<double>(OutSnapshot.Metadata.HeightScale), UE_DOUBLE_SMALL_NUMBER);
		OutError.Reset();
		return true;
	}

	bool EnsurePlannedRegions(FString* OutReason = nullptr)
	{
		FEonformTerrainDatasetSnapshot Snapshot;
		FEonformMeshTerrainOutputSettings Settings;
		FString Error;
		if (!ResolveCurrentBuildInput(Snapshot, Settings, Error))
		{
			if (OutReason) *OutReason = Error;
			return false;
		}

		const FEonformScalarField* Height = Snapshot.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			if (OutReason) *OutReason = TEXT("The evaluated terrain has no valid Height field.");
			return false;
		}

		const FIntPoint Resolution = Settings.TargetResolution.X > 1 && Settings.TargetResolution.Y > 1
			? Settings.TargetResolution
			: Height->Domain.Dimensions;
		const FEonformMeshTerrainLayoutEstimate Layout = FEonformMeshTerrainOutput::EstimateLayout(Resolution, Settings);
		if (!Layout.bValid)
		{
			if (OutReason) *OutReason = TEXT("The current Mesh Terrain region layout is invalid.");
			return false;
		}

		if (!FEonformTerrainRegionRegistry::BeginCurrentPlan(Resolution, Layout.Sections, &Error))
		{
			if (OutReason) *OutReason = Error;
			return false;
		}

		const int32 IntervalsX = Resolution.X - 1;
		const int32 IntervalsY = Resolution.Y - 1;
		for (int32 RegionY = 0; RegionY < Layout.Sections.Y; ++RegionY)
		{
			const int32 StartY = (RegionY * IntervalsY) / Layout.Sections.Y;
			const int32 EndY = ((RegionY + 1) * IntervalsY) / Layout.Sections.Y;
			for (int32 RegionX = 0; RegionX < Layout.Sections.X; ++RegionX)
			{
				const int32 StartX = (RegionX * IntervalsX) / Layout.Sections.X;
				const int32 EndX = ((RegionX + 1) * IntervalsX) / Layout.Sections.X;
				FEonformTerrainRegionRegistry::RegisterCurrentRegion(
					FIntPoint(RegionX, RegionY),
					FIntPoint(StartX, StartY),
					FIntPoint(EndX, EndY));
			}
		}

		if (OutReason) OutReason->Reset();
		return true;
	}
}

using namespace EonformTerrainRegionGridPrivate;

void SEonformTerrainRegionGrid::Construct(const FArguments& InArgs)
{
	RegionCellSize = DefaultRegionCellSize;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Mesh Terrain Regions")))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("-"))).OnClicked(this, &SEonformTerrainRegionGrid::ZoomOut)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
			[
				SNew(STextBlock).Text(this, &SEonformTerrainRegionGrid::GetZoomText)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("+"))).OnClicked(this, &SEonformTerrainRegionGrid::ZoomIn)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("100%"))).OnClicked(this, &SEonformTerrainRegionGrid::ResetZoom)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(this, &SEonformTerrainRegionGrid::GetLayoutSummaryText)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Text(this, &SEonformTerrainRegionGrid::GetSelectionText)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					SAssignNew(Grid, SUniformGridPanel).SlotPadding(FMargin(2.0f))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Generate Selected")))
				.IsEnabled(this, &SEonformTerrainRegionGrid::HasSelection)
				.OnClicked(this, &SEonformTerrainRegionGrid::RegenerateSelected)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Unload Selected")))
				.IsEnabled(this, &SEonformTerrainRegionGrid::HasSelection)
				.OnClicked(this, &SEonformTerrainRegionGrid::UnloadSelected)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Select Actors")))
				.IsEnabled(this, &SEonformTerrainRegionGrid::HasSelection)
				.OnClicked(this, &SEonformTerrainRegionGrid::SelectActorsForSelected)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Clear")))
				.IsEnabled(this, &SEonformTerrainRegionGrid::HasSelection)
				.OnClicked(this, &SEonformTerrainRegionGrid::ClearSelection)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(this, &SEonformTerrainRegionGrid::GetActionStatusText)
		]
	];
	Rebuild();
}

void SEonformTerrainRegionGrid::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SyncSelectionFromEditor();
	const uint64 Serial = FEonformTerrainRegionRegistry::GetChangeSerial();
	const uint64 PlanningSignature = GetPlanningSignature();
	if (Serial != LastChangeSerial || PlanningSignature != LastPlanningSignature)
	{
		Rebuild();
	}
}

FReply SEonformTerrainRegionGrid::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsControlDown())
	{
		if (MouseEvent.GetWheelDelta() > 0.0f) return ZoomIn();
		if (MouseEvent.GetWheelDelta() < 0.0f) return ZoomOut();
	}
	return SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
}

FReply SEonformTerrainRegionGrid::ZoomIn()
{
	RegionCellSize = FMath::Min(RegionCellSize + RegionZoomStep, MaxRegionCellSize);
	Rebuild();
	return FReply::Handled();
}

FReply SEonformTerrainRegionGrid::ZoomOut()
{
	RegionCellSize = FMath::Max(RegionCellSize - RegionZoomStep, MinRegionCellSize);
	Rebuild();
	return FReply::Handled();
}

FReply SEonformTerrainRegionGrid::ResetZoom()
{
	RegionCellSize = DefaultRegionCellSize;
	Rebuild();
	return FReply::Handled();
}

FText SEonformTerrainRegionGrid::GetZoomText() const
{
	return FText::FromString(FString::Printf(TEXT("%.0f%%"), (RegionCellSize / DefaultRegionCellSize) * 100.0f));
}

FReply SEonformTerrainRegionGrid::HandleRegionClicked(FIntPoint Coordinate)
{
	const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
	const bool bCtrl = Modifiers.IsControlDown();
	const bool bShift = Modifiers.IsShiftDown();
	if (bShift && bHasSelectionAnchor)
	{
		AddRectangularSelection(SelectionAnchor, Coordinate, bCtrl);
	}
	else if (bCtrl)
	{
		if (SelectedRegions.Contains(Coordinate)) SelectedRegions.Remove(Coordinate);
		else SelectedRegions.Add(Coordinate);
		SelectionAnchor = Coordinate;
		bHasSelectionAnchor = true;
	}
	else
	{
		SelectedRegions.Reset();
		SelectedRegions.Add(Coordinate);
		SelectionAnchor = Coordinate;
		bHasSelectionAnchor = true;
	}

	ActionStatus.Reset();
	ApplySelectionToEditor();
	Rebuild();
	return FReply::Handled();
}

void SEonformTerrainRegionGrid::AddRectangularSelection(
	const FIntPoint& A,
	const FIntPoint& B,
	bool bAdditive)
{
	if (!bAdditive) SelectedRegions.Reset();
	const int32 MinX = FMath::Max(0, FMath::Min(A.X, B.X));
	const int32 MinY = FMath::Max(0, FMath::Min(A.Y, B.Y));
	const int32 MaxX = FMath::Min(LatestGridDimensions.X - 1, FMath::Max(A.X, B.X));
	const int32 MaxY = FMath::Min(LatestGridDimensions.Y - 1, FMath::Max(A.Y, B.Y));
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X) SelectedRegions.Add(FIntPoint(X, Y));
	}
}

TArray<FIntPoint> SEonformTerrainRegionGrid::GetSelectedRegions() const
{
	TArray<FIntPoint> Result;
	Result.Reserve(SelectedRegions.Num());
	for (const FIntPoint& Coordinate : SelectedRegions) Result.Add(Coordinate);
	Result.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.Y == B.Y ? A.X < B.X : A.Y < B.Y;
	});
	return Result;
}

bool SEonformTerrainRegionGrid::HasSelection() const
{
	return SelectedRegions.Num() > 0;
}

FText SEonformTerrainRegionGrid::GetLayoutSummaryText() const
{
	return FText::FromString(LayoutSummary);
}

FText SEonformTerrainRegionGrid::GetSelectionText() const
{
	if (SelectedRegions.Num() == 0) return FText::FromString(TEXT("No regions selected."));
	if (SelectedRegions.Num() == 1)
	{
		const FIntPoint Coordinate = *SelectedRegions.CreateConstIterator();
		return FText::FromString(FString::Printf(TEXT("Selected %s."), *RegionLabel(Coordinate)));
	}
	return FText::FromString(FString::Printf(TEXT("%d regions selected."), SelectedRegions.Num()));
}

FText SEonformTerrainRegionGrid::GetActionStatusText() const
{
	return FText::FromString(ActionStatus);
}

FReply SEonformTerrainRegionGrid::RegenerateSelected()
{
	if (SelectedRegions.Num() == 0) return FReply::Handled();
	FEonformTerrainDatasetSnapshot Snapshot;
	FEonformMeshTerrainOutputSettings Settings;
	FString Error;
	if (!ResolveCurrentBuildInput(Snapshot, Settings, Error))
	{
		ActionStatus = Error;
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		return FReply::Handled();
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const FEonformMeshTerrainBuildResult Result = FEonformMeshTerrainOutput::BuildRegions(
		World,
		Snapshot.Dataset,
		Snapshot.Metadata.HeightScale,
		Settings,
		GetSelectedRegions());
	ActionStatus = Result.Message;
	if (!Result.bSuccess)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("EONFORM: %s"), *Result.Message);
		ApplySelectionToEditor();
	}
	Rebuild();
	return FReply::Handled();
}

FReply SEonformTerrainRegionGrid::UnloadSelected()
{
	if (SelectedRegions.Num() == 0) return FReply::Handled();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	FString Error;
	const int32 Count = FEonformMeshTerrainOutput::UnloadRegions(World, GetSelectedRegions(), &Error);
	if (!Error.IsEmpty())
	{
		ActionStatus = Error;
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
	}
	else
	{
		ActionStatus = FString::Printf(
			TEXT("Unloaded %d selected terrain region%s."),
			Count,
			Count == 1 ? TEXT("") : TEXT("s"));
	}
	ApplySelectionToEditor();
	Rebuild();
	return FReply::Handled();
}

FReply SEonformTerrainRegionGrid::SelectActorsForSelected()
{
	ApplySelectionToEditor();
	return FReply::Handled();
}

FReply SEonformTerrainRegionGrid::ClearSelection()
{
	SelectedRegions.Reset();
	LastEditorRegionSelection.Reset();
	bHasSelectionAnchor = false;
	ActionStatus.Reset();
	if (GEditor) GEditor->SelectNone(false, true, false);
	Rebuild();
	return FReply::Handled();
}

void SEonformTerrainRegionGrid::ApplySelectionToEditor()
{
	if (!GEditor) return;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	TSet<FIntPoint> MaterializedSelection;
	GEditor->SelectNone(false, true, false);
	for (const FIntPoint& Coordinate : SelectedRegions)
	{
		if (AActor* Actor = FEonformMeshTerrainOutput::FindRegionActor(World, Coordinate))
		{
			GEditor->SelectActor(Actor, true, false, true, true);
			MaterializedSelection.Add(Coordinate);
		}
	}
	GEditor->NoteSelectionChange(true);
	LastEditorRegionSelection = MoveTemp(MaterializedSelection);
}

void SEonformTerrainRegionGrid::SyncSelectionFromEditor()
{
	if (!GEditor) return;
	USelection* EditorSelection = GEditor->GetSelectedActors();
	if (!EditorSelection) return;

	TSet<FIntPoint> CurrentEditorRegions;
	for (FSelectionIterator It(*EditorSelection); It; ++It)
	{
		const AActor* Actor = Cast<AActor>(*It);
		FIntPoint Coordinate;
		if (FEonformMeshTerrainOutput::TryGetRegionIndex(Actor, Coordinate)) CurrentEditorRegions.Add(Coordinate);
	}
	if (SetsEqual(CurrentEditorRegions, LastEditorRegionSelection)) return;

	LastEditorRegionSelection = CurrentEditorRegions;
	if (CurrentEditorRegions.Num() == 0)
	{
		SelectedRegions.Reset();
		bHasSelectionAnchor = false;
	}
	else
	{
		SelectedRegions = CurrentEditorRegions;
		SelectionAnchor = *SelectedRegions.CreateConstIterator();
		bHasSelectionAnchor = true;
	}
	ActionStatus.Reset();
	Rebuild();
}

void SEonformTerrainRegionGrid::Rebuild()
{
	if (!Grid.IsValid()) return;

	const FMeshTerrainPreviewLayout Preview = ResolvePreviewLayout();
	LastPlanningSignature = GetPlanningSignature();
	LastChangeSerial = FEonformTerrainRegionRegistry::GetChangeSerial();
	Grid->ClearChildren();

	if (!Preview.bValid)
	{
		LatestGridDimensions = FIntPoint::ZeroValue;
		LayoutSummary = TEXT("Choose an output resolution to preview the Mesh Terrain regions.");
		Grid->AddSlot(0, 0)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("No region layout available.")))
		];
		return;
	}

	LatestGridDimensions = Preview.Regions;

	FString PlanningReason;
	EnsurePlannedRegions(&PlanningReason);

	FEonformTerrainRegionPlanIdentity Identity;
	TArray<FEonformTerrainRegionSnapshot> Regions;
	FEonformTerrainRegionRegistry::GetLatestPlan(Identity, Regions);
	TMap<FIntPoint, FEonformTerrainRegionSnapshot> SnapshotByRegion;
	for (const FEonformTerrainRegionSnapshot& Snapshot : Regions)
	{
		SnapshotByRegion.Add(Snapshot.RegionIndex, Snapshot);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TSet<FIntPoint> LoadedRegions;
	for (int32 RegionY = 0; RegionY < Preview.Regions.Y; ++RegionY)
	{
		for (int32 RegionX = 0; RegionX < Preview.Regions.X; ++RegionX)
		{
			const FIntPoint Region(RegionX, RegionY);
			if (World && FEonformMeshTerrainOutput::FindRegionActor(World, Region)) LoadedRegions.Add(Region);
		}
	}

	LayoutSummary = FString::Printf(
		TEXT("%d x %d regions | %d total | %d loaded | Ctrl+wheel to zoom"),
		Preview.Regions.X,
		Preview.Regions.Y,
		Preview.Regions.X * Preview.Regions.Y,
		LoadedRegions.Num());

	for (int32 RegionY = 0; RegionY < Preview.Regions.Y; ++RegionY)
	{
		for (int32 RegionX = 0; RegionX < Preview.Regions.X; ++RegionX)
		{
			const FIntPoint Region(RegionX, RegionY);
			const FEonformTerrainRegionSnapshot* RegionSnapshot = SnapshotByRegion.Find(Region);
			const bool bLoaded = LoadedRegions.Contains(Region);
			const bool bSelected = SelectedRegions.Contains(Region);
			const FString Label = RegionLabel(Region);
			const FString StateText = RegionStateText(RegionSnapshot, bLoaded);

			FString Tooltip = FString::Printf(
				TEXT("Region %s\nIndex [%d,%d]\n%s"),
				*Label,
				Region.X,
				Region.Y,
				bLoaded ? TEXT("Loaded in editor world") : TEXT("Not loaded"));
			if (RegionSnapshot)
			{
				Tooltip += FString::Printf(
					TEXT("\n%s / %s"),
					*MaterializationStateText(RegionSnapshot->MaterializationState),
					*EvaluationStateText(RegionSnapshot->EvaluationState));
				if (RegionSnapshot->Progress.IsMeasured())
				{
					Tooltip += FString::Printf(TEXT("\n%.0f%%"), RegionSnapshot->Progress.GetFraction() * 100.0);
				}
				if (RegionSnapshot->VertexCount > 0 || RegionSnapshot->TriangleCount > 0)
				{
					Tooltip += FString::Printf(
						TEXT("\n%d verts / %d tris"),
						RegionSnapshot->VertexCount,
						RegionSnapshot->TriangleCount);
				}
				if (!RegionSnapshot->Error.IsEmpty()) Tooltip += FString::Printf(TEXT("\n%s"), *RegionSnapshot->Error);
			}
			else if (!PlanningReason.IsEmpty())
			{
				Tooltip += FString::Printf(TEXT("\n%s"), *PlanningReason);
			}

			Grid->AddSlot(RegionX, RegionY)
			[
				SNew(SBox)
				.WidthOverride(RegionCellSize)
				.HeightOverride(RegionCellSize)
				[
					SNew(SButton)
					.ContentPadding(FMargin(4.0f))
					.ButtonColorAndOpacity(bSelected
						? FLinearColor(0.20f, 0.45f, 0.80f, 1.0f)
						: bLoaded
							? FLinearColor(0.20f, 0.62f, 0.28f, 1.0f)
							: StateText == TEXT("FAILED")
								? FLinearColor(0.70f, 0.18f, 0.18f, 1.0f)
								: FLinearColor(0.23f, 0.23f, 0.23f, 1.0f))
					.ToolTipText(FText::FromString(MoveTemp(Tooltip)))
					.OnClicked_Lambda([this, Region]() { return HandleRegionClicked(Region); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.0f).VAlign(VAlign_Center).HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Justification(ETextJustify::Center)
							.Text(FText::FromString(Label))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Justification(ETextJustify::Center)
							.Text(FText::FromString(StateText))
						]
					]
				]
			];
		}
	}
}
