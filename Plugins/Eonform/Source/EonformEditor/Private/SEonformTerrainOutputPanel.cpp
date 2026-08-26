#include "SEonformTerrainOutputPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "EonformTerrainDatasetRegistry.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainOutputEditorState.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr double OutputCentimetersPerKilometer = 100000.0;
	constexpr double OutputCentimetersPerMeter = 100.0;
}

void SEonformTerrainOutputPanel::InitializePresets()
{
	ResolutionPresets.Reset();
	static constexpr int32 Values[] = { 0, 127, 253, 505, 1009, 2017, 4033, 8129 };
	for (const int32 Value : Values)
	{
		ResolutionPresets.Add(MakeShared<int32>(Value));
	}

	SectionLayoutPresets =
	{
		MakeShared<EEonformTerrainOutputSectionLayout>(EEonformTerrainOutputSectionLayout::Automatic),
		MakeShared<EEonformTerrainOutputSectionLayout>(EEonformTerrainOutputSectionLayout::Explicit)
	};

	SectionComplexityPresets =
	{
		MakeShared<EEonformTerrainOutputComplexity>(EEonformTerrainOutputComplexity::Responsive),
		MakeShared<EEonformTerrainOutputComplexity>(EEonformTerrainOutputComplexity::Balanced),
		MakeShared<EEonformTerrainOutputComplexity>(EEonformTerrainOutputComplexity::Detailed),
		MakeShared<EEonformTerrainOutputComplexity>(EEonformTerrainOutputComplexity::Maximum)
	};
}

FText SEonformTerrainOutputPanel::GetResolutionLabel(int32 Resolution) const
{
	return Resolution > 0
		? FText::FromString(FString::Printf(TEXT("%d x %d"), Resolution, Resolution))
		: FText::FromString(TEXT("Native / Source Resolution"));
}

FText SEonformTerrainOutputPanel::GetSectionLayoutLabel(EEonformTerrainOutputSectionLayout Layout) const
{
	return FText::FromString(Layout == EEonformTerrainOutputSectionLayout::Explicit ? TEXT("Explicit") : TEXT("Automatic"));
}

FText SEonformTerrainOutputPanel::GetSectionComplexityLabel(EEonformTerrainOutputComplexity Complexity) const
{
	switch (Complexity)
	{
	case EEonformTerrainOutputComplexity::Responsive: return FText::FromString(TEXT("Responsive (~32K tris / region)"));
	case EEonformTerrainOutputComplexity::Detailed: return FText::FromString(TEXT("Detailed (~524K tris / region)"));
	case EEonformTerrainOutputComplexity::Maximum: return FText::FromString(TEXT("Maximum (~2M tris / region)"));
	case EEonformTerrainOutputComplexity::Balanced:
	default: return FText::FromString(TEXT("Balanced (~131K tris / region)"));
	}
}

TSharedPtr<int32> SEonformTerrainOutputPanel::FindResolutionPreset(int32 Resolution) const
{
	for (const TSharedPtr<int32>& Preset : ResolutionPresets)
	{
		if (Preset.IsValid() && *Preset == Resolution) return Preset;
	}
	return ResolutionPresets.IsEmpty() ? nullptr : ResolutionPresets[0];
}

TSharedPtr<EEonformTerrainOutputSectionLayout> SEonformTerrainOutputPanel::FindSectionLayoutPreset(EEonformTerrainOutputSectionLayout Layout) const
{
	for (const TSharedPtr<EEonformTerrainOutputSectionLayout>& Preset : SectionLayoutPresets)
	{
		if (Preset.IsValid() && *Preset == Layout) return Preset;
	}
	return SectionLayoutPresets.IsEmpty() ? nullptr : SectionLayoutPresets[0];
}

TSharedPtr<EEonformTerrainOutputComplexity> SEonformTerrainOutputPanel::FindSectionComplexityPreset(EEonformTerrainOutputComplexity Complexity) const
{
	for (const TSharedPtr<EEonformTerrainOutputComplexity>& Preset : SectionComplexityPresets)
	{
		if (Preset.IsValid() && *Preset == Complexity) return Preset;
	}
	return SectionComplexityPresets.IsEmpty() ? nullptr : SectionComplexityPresets[0];
}

FIntPoint SEonformTerrainOutputPanel::ResolveOutputResolution() const
{
	const FEonformTerrainGraphOutputSettings& State = FEonformTerrainOutputEditorState::Get().GetSettings();
	if (State.OutputResolution > 0)
	{
		return FIntPoint(State.OutputResolution, State.OutputResolution);
	}

	FIntPoint NativeResolution;
	return FEonformTerrainDatasetRegistry::GetHeightResolution(TEXT("EonformGraph"), NativeResolution)
		? NativeResolution
		: FIntPoint::ZeroValue;
}

FEonformMeshTerrainOutputSettings SEonformTerrainOutputPanel::MakeMeshTerrainSettings() const
{
	const FEonformTerrainGraphOutputSettings& State = FEonformTerrainOutputEditorState::Get().GetSettings();
	FEonformMeshTerrainOutputSettings Settings;
	Settings.TargetResolution = State.OutputResolution > 0
		? FIntPoint(State.OutputResolution, State.OutputResolution)
		: FIntPoint::ZeroValue;
	Settings.SectionLayout = State.SectionLayout == EEonformTerrainOutputSectionLayout::Explicit
		? EEonformMeshTerrainSectionLayout::Explicit
		: EEonformMeshTerrainSectionLayout::Automatic;

	switch (State.SectionComplexity)
	{
	case EEonformTerrainOutputComplexity::Responsive: Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Responsive; break;
	case EEonformTerrainOutputComplexity::Detailed: Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Detailed; break;
	case EEonformTerrainOutputComplexity::Maximum: Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Maximum; break;
	case EEonformTerrainOutputComplexity::Balanced:
	default: Settings.SectionComplexity = EEonformMeshTerrainSectionComplexity::Balanced; break;
	}

	Settings.Sections = FIntPoint(State.SectionsX, State.SectionsY);
	Settings.MeshPartitionDefinition = State.MeshPartitionDefinition.IsValid()
		? State.MeshPartitionDefinition.TryLoad()
		: nullptr;
	return Settings;
}

bool SEonformTerrainOutputPanel::ApplyPhysicalScale(
	const FEonformTerrainDatasetSnapshot& Snapshot,
	FEonformMeshTerrainOutputSettings& InOutSettings,
	FString& OutError) const
{
	const FEonformScalarField* Height = Snapshot.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
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

	const FEonformTerrainGraphOutputSettings& State = FEonformTerrainOutputEditorState::Get().GetSettings();
	const double TargetWidthCm = State.WorldWidthKilometers * OutputCentimetersPerKilometer;
	const double TargetDepthCm = State.WorldDepthKilometers * OutputCentimetersPerKilometer;
	const double TargetElevationCm = State.ElevationScaleMeters * OutputCentimetersPerMeter;

	InOutSettings.HorizontalScale = 1.0;
	InOutSettings.HorizontalScaleXY = FVector2d(TargetWidthCm / SourceWidth, TargetDepthCm / SourceDepth);
	InOutSettings.VerticalScale = TargetElevationCm / FMath::Max(static_cast<double>(Snapshot.Metadata.HeightScale), UE_DOUBLE_SMALL_NUMBER);
	OutError.Reset();
	return true;
}

FText SEonformTerrainOutputPanel::GetOutputEstimateText() const
{
	const FEonformTerrainGraphOutputSettings& State = FEonformTerrainOutputEditorState::Get().GetSettings();
	const FIntPoint Resolution = ResolveOutputResolution();
	if (Resolution.X < 2 || Resolution.Y < 2)
	{
		return FText::FromString(TEXT("Connect a terrain output to see world and Mesh Terrain diagnostics."));
	}

	const FEonformMeshTerrainLayoutEstimate Estimate = FEonformMeshTerrainOutput::EstimateLayout(Resolution, MakeMeshTerrainSettings());
	if (!Estimate.bValid)
	{
		return FText::FromString(TEXT("The current Mesh Terrain layout cannot be resolved."));
	}

	const double SpacingXMetres = State.WorldWidthKilometers * 1000.0 / static_cast<double>(Resolution.X - 1);
	const double SpacingYMetres = State.WorldDepthKilometers * 1000.0 / static_cast<double>(Resolution.Y - 1);

	FString Diagnostics = FString::Printf(
		TEXT("World: %.3f x %.3f km   Elevation scale: %.1f m   Sea level: 0 m\n")
		TEXT("Resolution: %d x %d   Sample spacing: %.2f x %.2f m\n")
		TEXT("Regions: %d x %d (%lld)   Max region: %d x %d samples, %lld tris\n")
		TEXT("Total mesh: ~%lld vertices, %lld triangles"),
		State.WorldWidthKilometers,
		State.WorldDepthKilometers,
		State.ElevationScaleMeters,
		Resolution.X,
		Resolution.Y,
		SpacingXMetres,
		SpacingYMetres,
		Estimate.Sections.X,
		Estimate.Sections.Y,
		static_cast<long long>(Estimate.SectionCount),
		Estimate.MaxSectionResolution.X,
		Estimate.MaxSectionResolution.Y,
		static_cast<long long>(Estimate.MaxSectionTriangleCount),
		static_cast<long long>(Estimate.TotalVertexCount),
		static_cast<long long>(Estimate.TotalTriangleCount));

	FEonformTerrainHeightStatistics HeightStatistics;
	if (FEonformTerrainDatasetRegistry::GetHeightStatistics(TEXT("EonformGraph"), HeightStatistics))
	{
		const double MinimumMetres = HeightStatistics.Minimum * State.ElevationScaleMeters;
		const double MaximumMetres = HeightStatistics.Maximum * State.ElevationScaleMeters;
		const double MeanMetres = HeightStatistics.Mean * State.ElevationScaleMeters;
		const double LandPercent = HeightStatistics.GetLandFraction() * 100.0;
		const double UnderwaterPercent = HeightStatistics.GetUnderwaterFraction() * 100.0;
		const double DatumPercent = HeightStatistics.SampleCount > 0
			? static_cast<double>(HeightStatistics.SeaLevelSampleCount) * 100.0 / static_cast<double>(HeightStatistics.SampleCount)
			: 0.0;

		Diagnostics += FString::Printf(
			TEXT("\nElevation: %.1f m to %.1f m   Mean: %.1f m")
			TEXT("\nCoverage: %.1f%% land   %.1f%% underwater   %.2f%% at sea datum"),
			MinimumMetres,
			MaximumMetres,
			MeanMetres,
			LandPercent,
			UnderwaterPercent,
			DatumPercent);

		if (!HeightStatistics.CrossesSeaLevel())
		{
			Diagnostics += HeightStatistics.UnderwaterSampleCount > 0
				? TEXT("\nNOTE: Current terrain is entirely at or below sea level.")
				: TEXT("\nNOTE: Current terrain contains no bathymetry below sea level.");
		}
	}

	const double MaxSpacingMetres = FMath::Max(SpacingXMetres, SpacingYMetres);
	if (MaxSpacingMetres > 250.0)
	{
		Diagnostics += TEXT("\nWARNING: Sample spacing exceeds 250 m; this output is macro-scale and will lose local terrain structure.");
	}
	else if (MaxSpacingMetres > 100.0)
	{
		Diagnostics += TEXT("\nWARNING: Sample spacing exceeds 100 m; fine drainage, cliffs and local erosion features will be under-resolved.");
	}

	if (Estimate.TotalTriangleCount > 100000000ll)
	{
		Diagnostics += TEXT("\nWARNING: Output exceeds 100 million triangles before Mesh Partition compilation. Consider a lower output resolution.");
	}
	else if (Estimate.TotalTriangleCount > 50000000ll)
	{
		Diagnostics += TEXT("\nWARNING: Output exceeds 50 million triangles; generation and editor memory cost will be substantial.");
	}

	return FText::FromString(MoveTemp(Diagnostics));
}

FReply SEonformTerrainOutputPanel::EditMeshPartitionDefinition()
{
	const FSoftObjectPath& Path = FEonformTerrainOutputEditorState::Get().GetSettings().MeshPartitionDefinition;
	if (Path.IsValid() && GEditor)
	{
		if (UObject* Asset = Path.TryLoad())
		{
			if (UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				Subsystem->OpenEditorForAsset(Asset);
			}
		}
	}
	return FReply::Handled();
}

FText SEonformTerrainOutputPanel::GetGenerateButtonText() const
{
	if (bGenerateQueued)
	{
		return FText::FromString(TEXT("Generate Terrain (waiting for terrain...)"));
	}
	return FText::FromString(TEXT("Generate Terrain"));
}

FReply SEonformTerrainOutputPanel::GenerateTerrain()
{
	FEonformTerrainOutputEditorState& State = FEonformTerrainOutputEditorState::Get();

	// A published base terrain is generation-safe even while derived hydrology is
	// still running. Do not make the output button wait for analysis enrichment.
	if (State.IsAnalysisAvailable())
	{
		if (!GenerateAvailableTerrain()) bGenerateQueued = true;
		return FReply::Handled();
	}

	if (!State.GetAnalysisError().IsEmpty() && !State.IsAnalysisPending())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
			TEXT("EONFORM terrain evaluation failed: %s"),
			*State.GetAnalysisError())));
		return FReply::Handled();
	}

	// The graph evaluator has not produced terrain yet. Remember the user's click
	// and fulfil it automatically as soon as the first valid final revision lands.
	bGenerateQueued = true;
	return FReply::Handled();
}

bool SEonformTerrainOutputPanel::GenerateAvailableTerrain()
{
	const FEonformTerrainOutputEditorState& State = FEonformTerrainOutputEditorState::Get();
	if (!State.IsAnalysisAvailable())
	{
		return false;
	}

	FEonformTerrainDatasetSnapshot Snapshot;
	if (!FEonformTerrainDatasetRegistry::Get(TEXT("EonformGraph"), Snapshot)
		|| !Snapshot.IsValid()
		|| Snapshot.Revision != State.GetPublishedAnalysisRevision())
	{
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("The editor world is not available.")));
		return true;
	}

	FEonformMeshTerrainOutputSettings Settings = MakeMeshTerrainSettings();
	FString ScaleError;
	if (!ApplyPhysicalScale(Snapshot, Settings, ScaleError))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ScaleError));
		return true;
	}

	const FEonformMeshTerrainBuildResult Result = FEonformMeshTerrainOutput::Build(
		World,
		Snapshot.Dataset,
		Snapshot.Metadata.HeightScale,
		Settings);
	if (!Result.bSuccess)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
		return true;
	}

	UE_LOG(LogTemp, Display, TEXT("EONFORM: %s"), *Result.Message);
	if (Result.TerrainActor && GEditor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Result.TerrainActor.Get(), true, true, true, true);
	}
	return true;
}

void SEonformTerrainOutputPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bGenerateQueued) return;

	FEonformTerrainOutputEditorState& State = FEonformTerrainOutputEditorState::Get();

	// The first valid terrain revision is enough to honour the queued request.
	// Hydrology may still be enriching the published snapshot in parallel.
	if (State.IsAnalysisAvailable())
	{
		if (GenerateAvailableTerrain()) bGenerateQueued = false;
		return;
	}

	if (State.IsAnalysisPending()) return;

	if (!State.GetAnalysisError().IsEmpty())
	{
		bGenerateQueued = false;
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
			TEXT("EONFORM terrain evaluation failed: %s"),
			*State.GetAnalysisError())));
	}
}

void SEonformTerrainOutputPanel::Construct(const FArguments& InArgs)
{
	InitializePresets();
	FEonformTerrainOutputEditorState& State = FEonformTerrainOutputEditorState::Get();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(8.0f, 6.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Terrain Output")))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(FEonformMeshTerrainOutput::GetMeshPartitionDefinitionClass())
				.ObjectPath_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().MeshPartitionDefinition.ToString(); })
				.OnObjectChanged_Lambda([](const FAssetData& AssetData)
				{
					FEonformTerrainOutputEditorState::Get().SetMeshPartitionDefinition(
						AssetData.IsValid() ? AssetData.GetSoftObjectPath() : FSoftObjectPath());
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("Edit MPD"))).OnClicked(this, &SEonformTerrainOutputPanel::EditMeshPartitionDefinition)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("World Dimensions")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SNumericEntryBox<double>)
					.Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Width km")))]
					.Value_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().WorldWidthKilometers; })
					.MinValue(0.001)
					.OnValueChanged_Lambda([](double V) { FEonformTerrainOutputEditorState::Get().SetWorldWidthKilometers(V); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SNumericEntryBox<double>)
					.Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Depth km")))]
					.Value_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().WorldDepthKilometers; })
					.MinValue(0.001)
					.OnValueChanged_Lambda([](double V) { FEonformTerrainOutputEditorState::Get().SetWorldDepthKilometers(V); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SNumericEntryBox<double>)
					.Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Elevation m")))]
					.Value_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().ElevationScaleMeters; })
					.MinValue(0.001)
					.OnValueChanged_Lambda([](double V) { FEonformTerrainOutputEditorState::Get().SetElevationScaleMeters(V); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Sea Level  0 m (fixed)")))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Mesh Terrain")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<int32>>)
				.OptionsSource(&ResolutionPresets)
				.InitiallySelectedItem(FindResolutionPreset(State.GetSettings().OutputResolution))
				.OnGenerateWidget_Lambda([this](TSharedPtr<int32> Item) { return SNew(STextBlock).Text(GetResolutionLabel(Item.IsValid() ? *Item : 0)); })
				.OnSelectionChanged_Lambda([](TSharedPtr<int32> Item, ESelectInfo::Type) { if (Item.IsValid()) FEonformTerrainOutputEditorState::Get().SetOutputResolution(*Item); })
				[
					SNew(STextBlock).Text_Lambda([this]() { return GetResolutionLabel(FEonformTerrainOutputEditorState::Get().GetSettings().OutputResolution); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<EEonformTerrainOutputSectionLayout>>)
				.OptionsSource(&SectionLayoutPresets)
				.InitiallySelectedItem(FindSectionLayoutPreset(State.GetSettings().SectionLayout))
				.OnGenerateWidget_Lambda([this](TSharedPtr<EEonformTerrainOutputSectionLayout> Item) { return SNew(STextBlock).Text(GetSectionLayoutLabel(Item.IsValid() ? *Item : EEonformTerrainOutputSectionLayout::Automatic)); })
				.OnSelectionChanged_Lambda([](TSharedPtr<EEonformTerrainOutputSectionLayout> Item, ESelectInfo::Type) { if (Item.IsValid()) FEonformTerrainOutputEditorState::Get().SetSectionLayout(*Item); })
				[
					SNew(STextBlock).Text_Lambda([this]() { return GetSectionLayoutLabel(FEonformTerrainOutputEditorState::Get().GetSettings().SectionLayout); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().SectionLayout == EEonformTerrainOutputSectionLayout::Automatic ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SComboBox<TSharedPtr<EEonformTerrainOutputComplexity>>)
					.OptionsSource(&SectionComplexityPresets)
					.InitiallySelectedItem(FindSectionComplexityPreset(State.GetSettings().SectionComplexity))
					.OnGenerateWidget_Lambda([this](TSharedPtr<EEonformTerrainOutputComplexity> Item) { return SNew(STextBlock).Text(GetSectionComplexityLabel(Item.IsValid() ? *Item : EEonformTerrainOutputComplexity::Balanced)); })
					.OnSelectionChanged_Lambda([](TSharedPtr<EEonformTerrainOutputComplexity> Item, ESelectInfo::Type) { if (Item.IsValid()) FEonformTerrainOutputEditorState::Get().SetSectionComplexity(*Item); })
					[
						SNew(STextBlock).Text_Lambda([this]() { return GetSectionComplexityLabel(FEonformTerrainOutputEditorState::Get().GetSettings().SectionComplexity); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().SectionLayout == EEonformTerrainOutputSectionLayout::Explicit ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SNumericEntryBox<int32>).Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Sections X")))].Value_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().SectionsX; }).MinValue(1).OnValueChanged_Lambda([](int32 V) { FEonformTerrainOutputEditorState::Get().SetSectionsX(V); })
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SNumericEntryBox<int32>).Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Sections Y")))].Value_Lambda([]() { return FEonformTerrainOutputEditorState::Get().GetSettings().SectionsY; }).MinValue(1).OnValueChanged_Lambda([](int32 V) { FEonformTerrainOutputEditorState::Get().SetSectionsY(V); })
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).AutoWrapText(true).Text(this, &SEonformTerrainOutputPanel::GetOutputEstimateText)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SButton).Text(this, &SEonformTerrainOutputPanel::GetGenerateButtonText).OnClicked(this, &SEonformTerrainOutputPanel::GenerateTerrain)
			]
		]
	];
}
