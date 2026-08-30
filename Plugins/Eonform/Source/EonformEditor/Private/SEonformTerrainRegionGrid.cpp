#include "SEonformTerrainRegionGrid.h"

#include "EonformTerrainRegionRegistry.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
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
		if (!Snapshot.Error.IsEmpty())
		{
			Text += TEXT("\nERROR");
		}
		return FText::FromString(MoveTemp(Text));
	}
}

void SEonformTerrainRegionGrid::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Regional Terrain Status")))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(Grid, SUniformGridPanel).SlotPadding(FMargin(2.0f))
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
	const uint64 Serial = FEonformTerrainRegionRegistry::GetChangeSerial();
	if (Serial != LastChangeSerial)
	{
		Rebuild();
	}
}

void SEonformTerrainRegionGrid::Rebuild()
{
	LastChangeSerial = FEonformTerrainRegionRegistry::GetChangeSerial();
	if (!Grid.IsValid()) return;
	Grid->ClearChildren();

	FEonformTerrainRegionPlanIdentity Identity;
	TArray<FEonformTerrainRegionSnapshot> Regions;
	if (!FEonformTerrainRegionRegistry::GetLatestPlan(Identity, Regions) || Regions.IsEmpty())
	{
		Grid->AddSlot(0, 0)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Generate regional terrain to populate status.")))
		];
		return;
	}

	for (const FEonformTerrainRegionSnapshot& Snapshot : Regions)
	{
		Grid->AddSlot(Snapshot.RegionIndex.X, Snapshot.RegionIndex.Y)
		[
			SNew(SBorder)
			.Padding(FMargin(6.0f))
			.ToolTipText(Snapshot.Error.IsEmpty() ? FText::GetEmpty() : FText::FromString(Snapshot.Error))
			[
				SNew(STextBlock).Text(MakeRegionText(Snapshot))
			]
		];
	}
}
