#include "SEonformTerrainRegionGrid.h"

#include "EonformTerrainRegionRegistry.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString ResidencyLabel(EEonformTerrainRegionResidency Residency)
	{
		switch (Residency)
		{
		case EEonformTerrainRegionResidency::Loading: return TEXT("Loading");
		case EEonformTerrainRegionResidency::Loaded: return TEXT("Loaded");
		case EEonformTerrainRegionResidency::Evicting: return TEXT("Evicting");
		case EEonformTerrainRegionResidency::Unloaded:
		default: return TEXT("Unloaded");
		}
	}

	FString StageLabel(EEonformTerrainRegionStage Stage)
	{
		switch (Stage)
		{
		case EEonformTerrainRegionStage::Queued: return TEXT("Queued");
		case EEonformTerrainRegionStage::Generating: return TEXT("Generating");
		case EEonformTerrainRegionStage::Meshing: return TEXT("Meshing");
		case EEonformTerrainRegionStage::Committing: return TEXT("Committing");
		case EEonformTerrainRegionStage::Resident: return TEXT("Resident");
		case EEonformTerrainRegionStage::Failed: return TEXT("Failed");
		case EEonformTerrainRegionStage::Idle:
		default: return TEXT("Idle");
		}
	}

	FString ResolutionLabel(const FEonformTerrainRegionSnapshot& Snapshot)
	{
		const FString Target = FString::Printf(TEXT("%dx%d"), Snapshot.TargetResolution.X, Snapshot.TargetResolution.Y);
		if (!Snapshot.bHasResidentResolution)
		{
			return FString::Printf(TEXT("Target %s"), *Target);
		}

		const FString Resident = FString::Printf(TEXT("%dx%d"), Snapshot.ResidentResolution.X, Snapshot.ResidentResolution.Y);
		return Snapshot.ResidentResolution == Snapshot.TargetResolution
			? Resident
			: FString::Printf(TEXT("%s -> %s"), *Resident, *Target);
	}

	FString ProgressLabel(const FEonformTerrainRegionSnapshot& Snapshot)
	{
		if (Snapshot.HasMeasuredProgress())
		{
			return FString::Printf(TEXT("%.0f%%"), Snapshot.Progress.GetFraction() * 100.0);
		}
		if (Snapshot.Stage == EEonformTerrainRegionStage::Queued
			|| Snapshot.Stage == EEonformTerrainRegionStage::Generating
			|| Snapshot.Stage == EEonformTerrainRegionStage::Meshing
			|| Snapshot.Stage == EEonformTerrainRegionStage::Committing)
		{
			return TEXT("Progress pending");
		}
		return FString();
	}

	FString CellLabel(const FEonformTerrainRegionSnapshot& Snapshot)
	{
		FString Label = FString::Printf(
			TEXT("[%d,%d]\n%s / %s\n%s\nrev %llu"),
			Snapshot.Id.Coordinate.X,
			Snapshot.Id.Coordinate.Y,
			*ResidencyLabel(Snapshot.Residency),
			*StageLabel(Snapshot.Stage),
			*ResolutionLabel(Snapshot),
			static_cast<unsigned long long>(Snapshot.bHasResidentResolution ? Snapshot.ResidentRevision : Snapshot.RequestedRevision));

		const FString Progress = ProgressLabel(Snapshot);
		if (!Progress.IsEmpty())
		{
			Label += TEXT("\n");
			Label += Progress;
		}
		if (Snapshot.bDirty)
		{
			Label += TEXT("\nStale");
		}
		if (!Snapshot.Error.IsEmpty())
		{
			Label += TEXT("\nError");
		}
		return Label;
	}
}

void SEonformTerrainRegionGrid::Construct(const FArguments& InArgs)
{
	SourceId = InArgs._SourceId;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Terrain Regions")))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(GridPanel, SUniformGridPanel)
			.SlotPadding(FMargin(2.0f))
		]
	];

	Refresh();
}

void SEonformTerrainRegionGrid::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const uint32 SnapshotHash = BuildSnapshotHash();
	if (!bHasSnapshotHash || SnapshotHash != LastSnapshotHash)
	{
		Refresh();
	}
}

uint32 SEonformTerrainRegionGrid::BuildSnapshotHash() const
{
	TArray<FEonformTerrainRegionSnapshot> Snapshots;
	FEonformTerrainRegionRegistry::GetSourceRegions(SourceId, Snapshots);

	uint32 Hash = GetTypeHash(Snapshots.Num());
	for (const FEonformTerrainRegionSnapshot& Snapshot : Snapshots)
	{
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.Id));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.GridDimensions));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Snapshot.Residency)));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Snapshot.Stage)));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.TargetResolution));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.ResidentResolution));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.RequestedRevision));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.ResidentRevision));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.Progress.CompletedWork));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.Progress.TotalWork));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.bDirty));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.Error));
	}
	return Hash;
}

void SEonformTerrainRegionGrid::Refresh()
{
	if (!GridPanel.IsValid())
	{
		return;
	}

	GridPanel->ClearChildren();
	TArray<FEonformTerrainRegionSnapshot> Snapshots;
	FEonformTerrainRegionRegistry::GetSourceRegions(SourceId, Snapshots);

	if (Snapshots.IsEmpty())
	{
		GridPanel->AddSlot(0, 0)
		[
			SNew(SBorder)
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(TEXT("Generate terrain to populate region status.")))
			]
		];
	}
	else
	{
		for (const FEonformTerrainRegionSnapshot& Snapshot : Snapshots)
		{
			GridPanel->AddSlot(Snapshot.Id.Coordinate.X, Snapshot.Id.Coordinate.Y)
			[
				SNew(SBorder)
				.Padding(5.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(false)
					.Text(FText::FromString(CellLabel(Snapshot)))
					.ToolTipText(FText::FromString(Snapshot.Error.IsEmpty() ? CellLabel(Snapshot) : Snapshot.Error))
				]
			];
		}
	}

	LastSnapshotHash = BuildSnapshotHash();
	bHasSnapshotHash = true;
}
