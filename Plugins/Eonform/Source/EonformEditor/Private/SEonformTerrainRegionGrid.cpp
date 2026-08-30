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
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace EonformTerrainRegionGridPrivate
{
	constexpr double OutputCentimetersPerKilometer = 100000.0;
	constexpr double OutputCentimetersPerMeter = 100.0;

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

	FText MakeRegionText(const FEonformTerrainRegionSnapshot& Snapshot)
	{
		const FIntPoint Resolution = Snapshot.GetRegionResolution();
		FString Text = FString::Printf(
			TEXT("[%d,%d]\n%s / %s\n%d x %d samples\nref %d x %d"),
			Snapshot.RegionIndex.X,
			Snapshot.RegionIndex.Y,
			*MaterializationStateText(Snapshot.MaterializationState),
			*EvaluationStateText(Snapshot.EvaluationState),
			Resolution.X,
			Resolution.Y,
			Snapshot.Key.ReferenceResolution.X,
			Snapshot.Key.ReferenceResolution.Y);

		if (Snapshot.Progress.IsMeasured())
		{
			Text += FString::Printf(TEXT("\n%.0f%%"), Snapshot.Progress.GetFraction() * 100.0);
		}
		else if (Snapshot.EvaluationState == EEonformTerrainRegionEvaluationState::Evaluating
			|| Snapshot.MaterializationState == EEonformTerrainRegionMaterializationState::Committing)
		{
			Text += TEXT("\nProgress pending");
		}

		if (Snapshot.VertexCount > 0 || Snapshot.TriangleCount > 0)
		{
			Text += FString::Printf(TEXT("\n%d verts / %d tris"), Snapshot.VertexCount, Snapshot.TriangleCount);
		}
		if (!Snapshot.Error.IsEmpty()) Text += TEXT("\nERROR");
		return FText::FromString(MoveTemp(Text));
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
		const FEonformTerrainGraphOutputSettings& Settings = State.GetSettings();
		uint64 Signature = State.GetPublishedAnalysisRevision();
		auto Mix = [&Signature](uint64 Value)
		{
			Signature ^= Value + 0x9e3779b97f4a7c15ull + (Signature << 6) + (Signature >> 2);
		};
		Mix(static_cast<uint64>(Settings.OutputResolution));
		Mix(static_cast<uint64>(Settings.SectionsX));
		Mix(static_cast<uint64>(Settings.SectionsY));
		Mix(static_cast<uint64>(Settings.SectionLayout));
		Mix(static_cast<uint64>(Settings.SectionComplexity));
		return Signature;
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
		OutSettings = FEonformMeshTerrainOutputSettings();
		OutSettings.TargetResolution = GraphSettings.OutputResolution > 0
			? FIntPoint(
				FMath::Clamp(GraphSettings.OutputResolution, 17, 4097),
				FMath::Clamp(GraphSettings.OutputResolution, 17, 4097))
			: FIntPoint::ZeroValue;
		OutSettings.SectionLayout = GraphSettings.SectionLayout == EEonformTerrainOutputSectionLayout::Explicit
			? EEonformMeshTerrainSectionLayout::Explicit
			: EEonformMeshTerrainSectionLayout::Automatic;
		switch (GraphSettings.SectionComplexity)
		{
		case EEonformTerrainOutputComplexity::Responsive:
			OutSettings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Responsive;
			break;
		case EEonformTerrainOutputComplexity::Detailed:
			OutSettings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Detailed;
			break;
		case EEonformTerrainOutputComplexity::Maximum:
			OutSettings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Maximum;
			break;
		case EEonformTerrainOutputComplexity::Balanced:
		default:
			OutSettings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Balanced;
			break;
		}
		OutSettings.Sections = FIntPoint(GraphSettings.SectionsX, GraphSettings.SectionsY);
		OutSettings.MeshPartitionDefinition = GraphSettings.MeshPartitionDefinition.IsValid()
			? GraphSettings.MeshPartitionDefinition.TryLoad()
			: nullptr;

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
		for (int32 SectionY = 0; SectionY < Layout.Sections.Y; ++SectionY)
		{
			const int32 StartY = (SectionY * IntervalsY) / Layout.Sections.Y;
			const int32 EndY = ((SectionY + 1) * IntervalsY) / Layout.Sections.Y;
			for (int32 SectionX = 0; SectionX < Layout.Sections.X; ++SectionX)
			{
				const int32 StartX = (SectionX * IntervalsX) / Layout.Sections.X;
				const int32 EndX = ((SectionX + 1) * IntervalsX) / Layout.Sections.X;
				FEonformTerrainRegionRegistry::RegisterCurrentRegion(
					FIntPoint(SectionX, SectionY),
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
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Regional Terrain Control")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(TEXT("Click a region to select it. Ctrl toggles regions; Shift selects a rectangle; Ctrl+Shift adds a rectangle.")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Text(this, &SEonformTerrainRegionGrid::GetSelectionText)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(Grid, SUniformGridPanel).SlotPadding(FMargin(2.0f))
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

FText SEonformTerrainRegionGrid::GetSelectionText() const
{
	if (SelectedRegions.Num() == 0) return FText::FromString(TEXT("No regions selected."));
	if (SelectedRegions.Num() == 1)
	{
		const FIntPoint Coordinate = *SelectedRegions.CreateConstIterator();
		return FText::FromString(FString::Printf(TEXT("Selected region [%d,%d]."), Coordinate.X, Coordinate.Y));
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

	FString PlanningReason;
	EnsurePlannedRegions(&PlanningReason);
	LastChangeSerial = FEonformTerrainRegionRegistry::GetChangeSerial();
	LastPlanningSignature = GetPlanningSignature();
	Grid->ClearChildren();

	FEonformTerrainRegionPlanIdentity Identity;
	TArray<FEonformTerrainRegionSnapshot> Regions;
	if (!FEonformTerrainRegionRegistry::GetLatestPlan(Identity, Regions) || Regions.IsEmpty())
	{
		LatestGridDimensions = FIntPoint::ZeroValue;
		Grid->AddSlot(0, 0)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(
				PlanningReason.IsEmpty()
					? TEXT("Connect a regional-compatible terrain graph to populate region controls.")
					: PlanningReason))
		];
		return;
	}
	LatestGridDimensions = Identity.GridDimensions;

	for (const FEonformTerrainRegionSnapshot& Snapshot : Regions)
	{
		const FIntPoint Coordinate = Snapshot.RegionIndex;
		Grid->AddSlot(Coordinate.X, Coordinate.Y)
		[
			SNew(SButton)
			.ContentPadding(FMargin(2.0f))
			.ButtonColorAndOpacity_Lambda([this, Coordinate]() -> FSlateColor
			{
				return FSlateColor(SelectedRegions.Contains(Coordinate)
					? FLinearColor(0.20f, 0.45f, 0.80f, 1.0f)
					: FLinearColor::White);
			})
			.ToolTipText(Snapshot.Error.IsEmpty() ? FText::GetEmpty() : FText::FromString(Snapshot.Error))
			.OnClicked_Lambda([this, Coordinate]() { return HandleRegionClicked(Coordinate); })
			[
				SNew(SBorder)
				.Padding(FMargin(5.0f))
				[
					SNew(STextBlock).Text(MakeRegionText(Snapshot))
				]
			]
		];
	}
}
