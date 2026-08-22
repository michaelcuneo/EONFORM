#include "SGaeaTerrainOutputPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainOutputEditorState.h"
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

void SGaeaTerrainOutputPanel::InitializePresets()
{
	ResolutionPresets.Reset();
	static constexpr int32 Values[] = { 0, 127, 253, 505, 1009, 2017, 4033, 8129 };
	for (const int32 Value : Values)
	{
		ResolutionPresets.Add(MakeShared<int32>(Value));
	}

	SectionLayoutPresets =
	{
		MakeShared<EGaeaTerrainOutputSectionLayout>(EGaeaTerrainOutputSectionLayout::Automatic),
		MakeShared<EGaeaTerrainOutputSectionLayout>(EGaeaTerrainOutputSectionLayout::Explicit)
	};

	SectionComplexityPresets =
	{
		MakeShared<EGaeaTerrainOutputComplexity>(EGaeaTerrainOutputComplexity::Responsive),
		MakeShared<EGaeaTerrainOutputComplexity>(EGaeaTerrainOutputComplexity::Balanced),
		MakeShared<EGaeaTerrainOutputComplexity>(EGaeaTerrainOutputComplexity::Detailed),
		MakeShared<EGaeaTerrainOutputComplexity>(EGaeaTerrainOutputComplexity::Maximum)
	};
}

FText SGaeaTerrainOutputPanel::GetResolutionLabel(int32 Resolution) const
{
	return Resolution > 0
		? FText::FromString(FString::Printf(TEXT("%d x %d"), Resolution, Resolution))
		: FText::FromString(TEXT("Native / Source Resolution"));
}

FText SGaeaTerrainOutputPanel::GetSectionLayoutLabel(EGaeaTerrainOutputSectionLayout Layout) const
{
	return FText::FromString(Layout == EGaeaTerrainOutputSectionLayout::Explicit ? TEXT("Explicit") : TEXT("Automatic"));
}

FText SGaeaTerrainOutputPanel::GetSectionComplexityLabel(EGaeaTerrainOutputComplexity Complexity) const
{
	switch (Complexity)
	{
	case EGaeaTerrainOutputComplexity::Responsive: return FText::FromString(TEXT("Responsive (~32K tris / region)"));
	case EGaeaTerrainOutputComplexity::Detailed: return FText::FromString(TEXT("Detailed (~524K tris / region)"));
	case EGaeaTerrainOutputComplexity::Maximum: return FText::FromString(TEXT("Maximum (~2M tris / region)"));
	case EGaeaTerrainOutputComplexity::Balanced:
	default: return FText::FromString(TEXT("Balanced (~131K tris / region)"));
	}
}

TSharedPtr<int32> SGaeaTerrainOutputPanel::FindResolutionPreset(int32 Resolution) const
{
	for (const TSharedPtr<int32>& Preset : ResolutionPresets)
	{
		if (Preset.IsValid() && *Preset == Resolution) return Preset;
	}
	return ResolutionPresets.IsEmpty() ? nullptr : ResolutionPresets[0];
}

TSharedPtr<EGaeaTerrainOutputSectionLayout> SGaeaTerrainOutputPanel::FindSectionLayoutPreset(EGaeaTerrainOutputSectionLayout Layout) const
{
	for (const TSharedPtr<EGaeaTerrainOutputSectionLayout>& Preset : SectionLayoutPresets)
	{
		if (Preset.IsValid() && *Preset == Layout) return Preset;
	}
	return SectionLayoutPresets.IsEmpty() ? nullptr : SectionLayoutPresets[0];
}

TSharedPtr<EGaeaTerrainOutputComplexity> SGaeaTerrainOutputPanel::FindSectionComplexityPreset(EGaeaTerrainOutputComplexity Complexity) const
{
	for (const TSharedPtr<EGaeaTerrainOutputComplexity>& Preset : SectionComplexityPresets)
	{
		if (Preset.IsValid() && *Preset == Complexity) return Preset;
	}
	return SectionComplexityPresets.IsEmpty() ? nullptr : SectionComplexityPresets[0];
}

FIntPoint SGaeaTerrainOutputPanel::ResolveOutputResolution() const
{
	const FGaeaTerrainGraphOutputSettings& State = FGaeaTerrainOutputEditorState::Get().GetSettings();
	if (State.OutputResolution > 0)
	{
		return FIntPoint(State.OutputResolution, State.OutputResolution);
	}

	FIntPoint NativeResolution;
	return FGaeaTerrainDatasetRegistry::GetHeightResolution(TEXT("CodenameGaeaGraph"), NativeResolution)
		? NativeResolution
		: FIntPoint::ZeroValue;
}

FGaeaMeshTerrainOutputSettings SGaeaTerrainOutputPanel::MakeMeshTerrainSettings() const
{
	const FGaeaTerrainGraphOutputSettings& State = FGaeaTerrainOutputEditorState::Get().GetSettings();
	FGaeaMeshTerrainOutputSettings Settings;
	Settings.TargetResolution = State.OutputResolution > 0
		? FIntPoint(State.OutputResolution, State.OutputResolution)
		: FIntPoint::ZeroValue;
	Settings.SectionLayout = State.SectionLayout == EGaeaTerrainOutputSectionLayout::Explicit
		? EGaeaMeshTerrainSectionLayout::Explicit
		: EGaeaMeshTerrainSectionLayout::Automatic;

	switch (State.SectionComplexity)
	{
	case EGaeaTerrainOutputComplexity::Responsive: Settings.SectionComplexity = EGaeaMeshTerrainSectionComplexity::Responsive; break;
	case EGaeaTerrainOutputComplexity::Detailed: Settings.SectionComplexity = EGaeaMeshTerrainSectionComplexity::Detailed; break;
	case EGaeaTerrainOutputComplexity::Maximum: Settings.SectionComplexity = EGaeaMeshTerrainSectionComplexity::Maximum; break;
	case EGaeaTerrainOutputComplexity::Balanced:
	default: Settings.SectionComplexity = EGaeaMeshTerrainSectionComplexity::Balanced; break;
	}

	Settings.Sections = FIntPoint(State.SectionsX, State.SectionsY);
	Settings.MeshPartitionDefinition = State.MeshPartitionDefinition.IsValid()
		? State.MeshPartitionDefinition.TryLoad()
		: nullptr;
	return Settings;
}

bool SGaeaTerrainOutputPanel::ApplyPhysicalScale(
	const FGaeaTerrainDatasetSnapshot& Snapshot,
	FGaeaMeshTerrainOutputSettings& InOutSettings,
	FString& OutError) const
{
	const FGaeaScalarField* Height = Snapshot.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
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

	const FGaeaTerrainGraphOutputSettings& State = FGaeaTerrainOutputEditorState::Get().GetSettings();
	const double TargetWidthCm = State.WorldWidthKilometers * OutputCentimetersPerKilometer;
	const double TargetDepthCm = State.WorldDepthKilometers * OutputCentimetersPerKilometer;
	const double TargetElevationCm = State.ElevationScaleMeters * OutputCentimetersPerMeter;

	InOutSettings.HorizontalScale = 1.0;
	InOutSettings.HorizontalScaleXY = FVector2d(TargetWidthCm / SourceWidth, TargetDepthCm / SourceDepth);
	InOutSettings.VerticalScale = TargetElevationCm / FMath::Max(static_cast<double>(Snapshot.Metadata.HeightScale), UE_DOUBLE_SMALL_NUMBER);
	OutError.Reset();
	return true;
}

FText SGaeaTerrainOutputPanel::GetOutputEstimateText() const
{
	const FGaeaTerrainGraphOutputSettings& State = FGaeaTerrainOutputEditorState::Get().GetSettings();
	const FIntPoint Resolution = ResolveOutputResolution();
	if (Resolution.X < 2 || Resolution.Y < 2)
	{
		return FText::FromString(TEXT("Connect a terrain output to see world and Mesh Terrain diagnostics."));
	}

	const FGaeaMeshTerrainLayoutEstimate Estimate = FGaeaMeshTerrainOutput::EstimateLayout(Resolution, MakeMeshTerrainSettings());
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

	FGaeaTerrainHeightStatistics HeightStatistics;
	if (FGaeaTerrainDatasetRegistry::GetHeightStatistics(TEXT("CodenameGaeaGraph"), HeightStatistics))
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

FReply SGaeaTerrainOutputPanel::EditMeshPartitionDefinition()
{
	const FSoftObjectPath& Path = FGaeaTerrainOutputEditorState::Get().GetSettings().MeshPartitionDefinition;
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

FText SGaeaTerrainOutputPanel::GetGenerateButtonText() const
{
	if (bGenerateQueued)
	{
		return FText::FromString(TEXT("Generate Terrain (waiting for analysis...)"));
	}
	return FText::FromString(TEXT("Generate Terrain"));
}

FReply SGaeaTerrainOutputPanel::GenerateTerrain()
{
	FGaeaTerrainOutputEditorState& State = FGaeaTerrainOutputEditorState::Get();

	if (!State.GetAnalysisError().IsEmpty() && !State.IsAnalysisPending())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
			TEXT("EONFORM terrain analysis failed: %s"),
			*State.GetAnalysisError())));
		return FReply::Handled();
	}

	if (State.IsAnalysisPending() || !State.IsAnalysisAvailable())
	{
		bGenerateQueued = true;
		return FReply::Handled();
	}

	if (!GenerateAvailableTerrain())
	{
		// Analysis state and registry publication are completed on the game thread,
		// but if they are observed between operations simply queue until the exact
		// published revision is visible. Never fall back to an older snapshot.
		bGenerateQueued = true;
	}
	return FReply::Handled();
}

bool SGaeaTerrainOutputPanel::GenerateAvailableTerrain()
{
	const FGaeaTerrainOutputEditorState& State = FGaeaTerrainOutputEditorState::Get();
	if (!State.IsAnalysisAvailable() || State.IsAnalysisPending())
	{
		return false;
	}

	FGaeaTerrainDatasetSnapshot Snapshot;
	if (!FGaeaTerrainDatasetRegistry::Get(TEXT("CodenameGaeaGraph"), Snapshot)
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

	FGaeaMeshTerrainOutputSettings Settings = MakeMeshTerrainSettings();
	FString ScaleError;
	if (!ApplyPhysicalScale(Snapshot, Settings, ScaleError))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ScaleError));
		return true;
	}

	const FGaeaMeshTerrainBuildResult Result = FGaeaMeshTerrainOutput::Build(
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

void SGaeaTerrainOutputPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bGenerateQueued) return;

	FGaeaTerrainOutputEditorState& State = FGaeaTerrainOutputEditorState::Get();
	if (State.IsAnalysisPending()) return;

	if (!State.GetAnalysisError().IsEmpty())
	{
		bGenerateQueued = false;
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
			TEXT("EONFORM terrain analysis failed: %s"),
			*State.GetAnalysisError())));
		return;
	}

	if (State.IsAnalysisAvailable() && GenerateAvailableTerrain())
	{
		bGenerateQueued = false;
	}
}

void SGaeaTerrainOutputPanel::Construct(const FArguments& InArgs)
{
	InitializePresets();
	FGaeaTerrainOutputEditorState& State = FGaeaTerrainOutputEditorState::Get();

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
				.AllowedClass(FGaeaMeshTerrainOutput::GetMeshPartitionDefinitionClass())
				.ObjectPath_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().MeshPartitionDefinition.ToString(); })
				.OnObjectChanged_Lambda([](const FAssetData& AssetData)
				{
					FGaeaTerrainOutputEditorState::Get().SetMeshPartitionDefinition(
						AssetData.IsValid() ? AssetData.GetSoftObjectPath() : FSoftObjectPath());
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton).Text(FText::FromString(TEXT("Edit MPD"))).OnClicked(this, &SGaeaTerrainOutputPanel::EditMeshPartitionDefinition)
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
					.Value_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().WorldWidthKilometers; })
					.MinValue(0.001)
					.OnValueChanged_Lambda([](double V) { FGaeaTerrainOutputEditorState::Get().SetWorldWidthKilometers(V); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SNumericEntryBox<double>)
					.Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Depth km")))]
					.Value_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().WorldDepthKilometers; })
					.MinValue(0.001)
					.OnValueChanged_Lambda([](double V) { FGaeaTerrainOutputEditorState::Get().SetWorldDepthKilometers(V); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SNumericEntryBox<double>)
					.Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Elevation m")))]
					.Value_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().ElevationScaleMeters; })
					.MinValue(0.001)
					.OnValueChanged_Lambda([](double V) { FGaeaTerrainOutputEditorState::Get().SetElevationScaleMeters(V); })
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
				.OnSelectionChanged_Lambda([](TSharedPtr<int32> Item, ESelectInfo::Type) { if (Item.IsValid()) FGaeaTerrainOutputEditorState::Get().SetOutputResolution(*Item); })
				[
					SNew(STextBlock).Text_Lambda([this]() { return GetResolutionLabel(FGaeaTerrainOutputEditorState::Get().GetSettings().OutputResolution); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<EGaeaTerrainOutputSectionLayout>>)
				.OptionsSource(&SectionLayoutPresets)
				.InitiallySelectedItem(FindSectionLayoutPreset(State.GetSettings().SectionLayout))
				.OnGenerateWidget_Lambda([this](TSharedPtr<EGaeaTerrainOutputSectionLayout> Item) { return SNew(STextBlock).Text(GetSectionLayoutLabel(Item.IsValid() ? *Item : EGaeaTerrainOutputSectionLayout::Automatic)); })
				.OnSelectionChanged_Lambda([](TSharedPtr<EGaeaTerrainOutputSectionLayout> Item, ESelectInfo::Type) { if (Item.IsValid()) FGaeaTerrainOutputEditorState::Get().SetSectionLayout(*Item); })
				[
					SNew(STextBlock).Text_Lambda([this]() { return GetSectionLayoutLabel(FGaeaTerrainOutputEditorState::Get().GetSettings().SectionLayout); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().SectionLayout == EGaeaTerrainOutputSectionLayout::Automatic ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SComboBox<TSharedPtr<EGaeaTerrainOutputComplexity>>)
					.OptionsSource(&SectionComplexityPresets)
					.InitiallySelectedItem(FindSectionComplexityPreset(State.GetSettings().SectionComplexity))
					.OnGenerateWidget_Lambda([this](TSharedPtr<EGaeaTerrainOutputComplexity> Item) { return SNew(STextBlock).Text(GetSectionComplexityLabel(Item.IsValid() ? *Item : EGaeaTerrainOutputComplexity::Balanced)); })
					.OnSelectionChanged_Lambda([](TSharedPtr<EGaeaTerrainOutputComplexity> Item, ESelectInfo::Type) { if (Item.IsValid()) FGaeaTerrainOutputEditorState::Get().SetSectionComplexity(*Item); })
					[
						SNew(STextBlock).Text_Lambda([this]() { return GetSectionComplexityLabel(FGaeaTerrainOutputEditorState::Get().GetSettings().SectionComplexity); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().SectionLayout == EGaeaTerrainOutputSectionLayout::Explicit ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SNumericEntryBox<int32>).Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Sections X")))].Value_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().SectionsX; }).MinValue(1).OnValueChanged_Lambda([](int32 V) { FGaeaTerrainOutputEditorState::Get().SetSectionsX(V); })
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SNumericEntryBox<int32>).Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Sections Y")))].Value_Lambda([]() { return FGaeaTerrainOutputEditorState::Get().GetSettings().SectionsY; }).MinValue(1).OnValueChanged_Lambda([](int32 V) { FGaeaTerrainOutputEditorState::Get().SetSectionsY(V); })
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).AutoWrapText(true).Text(this, &SGaeaTerrainOutputPanel::GetOutputEstimateText)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SButton).Text(this, &SGaeaTerrainOutputPanel::GetGenerateButtonText).OnClicked(this, &SGaeaTerrainOutputPanel::GenerateTerrain)
			]
		]
	];
}
