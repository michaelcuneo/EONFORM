#include "Modules/ModuleManager.h"

#include "AssetRegistry/AssetData.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "GaeaMeshTerrainOutput.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDynamicMeshActor.h"
#include "GaeaTerrainGraphNode.h"
#include "GaeaTerrainGraphPin.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SGaeaTerrainInspector.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCodenameGaeaEditorModule"

namespace
{
	const FName CodenameGaeaTabName(TEXT("CodenameGaea"));
}

class FCodenameGaeaEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		InitializeResolutionPresets();

		TerrainGraphNodeFactory = MakeShared<FGaeaTerrainGraphNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(TerrainGraphNodeFactory);

		TerrainGraphPinFactory = MakeShared<FGaeaTerrainGraphPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(TerrainGraphPinFactory);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			CodenameGaeaTabName,
			FOnSpawnTab::CreateRaw(this, &FCodenameGaeaEditorModule::SpawnCodenameGaeaTab))
			.SetDisplayName(LOCTEXT("CodenameGaeaTabTitle", "EONFORM"))
			.SetTooltipText(LOCTEXT("CodenameGaeaTabTooltip", "Open the EONFORM terrain inspector and output settings."))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCodenameGaeaEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		if (TerrainGraphPinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(TerrainGraphPinFactory);
			TerrainGraphPinFactory.Reset();
		}

		if (TerrainGraphNodeFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualNodeFactory(TerrainGraphNodeFactory);
			TerrainGraphNodeFactory.Reset();
		}

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CodenameGaeaTabName);
		}

		TerrainResolutionPresets.Reset();
	}

private:
	void InitializeResolutionPresets()
	{
		TerrainResolutionPresets.Reset();

		// Native keeps Mesh Terrain's arbitrary-resolution workflow intact.
		// The remaining entries are Epic's recommended Landscape-compatible
		// square heightfield dimensions in UE 5.8.
		static constexpr int32 Presets[] =
		{
			0,
			127,
			253,
			505,
			1009,
			2017,
			4033,
			8129
		};

		for (const int32 Resolution : Presets)
		{
			TerrainResolutionPresets.Add(MakeShared<int32>(Resolution));
		}
	}

	FText GetResolutionPresetLabel(int32 Resolution) const
	{
		if (Resolution <= 0)
		{
			return LOCTEXT("NativeResolutionPreset", "Native / Source Resolution");
		}

		return FText::FromString(FString::Printf(TEXT("%d x %d"), Resolution, Resolution));
	}

	TSharedPtr<int32> FindResolutionPreset(int32 Resolution) const
	{
		for (const TSharedPtr<int32>& Preset : TerrainResolutionPresets)
		{
			if (Preset.IsValid() && *Preset == Resolution)
			{
				return Preset;
			}
		}
		return TerrainResolutionPresets.Num() > 0 ? TerrainResolutionPresets[0] : nullptr;
	}

	FIntPoint GetSelectedTargetResolution() const
	{
		return TerrainResolutionPreset > 0
			? FIntPoint(TerrainResolutionPreset, TerrainResolutionPreset)
			: FIntPoint::ZeroValue;
	}

	bool GetLatestEvaluatedTerrain(FGaeaTerrainDatasetSnapshot& OutSnapshot) const
	{
		if (FGaeaTerrainDatasetRegistry::Get(TEXT("CodenameGaeaGraph"), OutSnapshot) && OutSnapshot.IsValid())
		{
			return true;
		}

		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("NoEvaluatedGaeaGraph", "No evaluated EONFORM graph is available. Evaluate the graph first."));
		return false;
	}

	UWorld* GetEditorWorld() const
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	FGaeaTerrainMeshBuildOptions MakePreviewOptions(float HeightScale) const
	{
		FGaeaTerrainMeshBuildOptions Options;
		Options.HeightScale = HeightScale;
		Options.HorizontalScale = TerrainHorizontalScale;
		Options.VerticalScale = TerrainVerticalScale;
		Options.TargetResolution = GetSelectedTargetResolution();
		return Options;
	}

	FGaeaMeshTerrainOutputSettings MakeMeshTerrainSettings() const
	{
		FGaeaMeshTerrainOutputSettings Settings;
		Settings.HorizontalScale = TerrainHorizontalScale;
		Settings.VerticalScale = TerrainVerticalScale;
		Settings.TargetResolution = GetSelectedTargetResolution();
		Settings.Sections = FIntPoint(TerrainSectionsX, TerrainSectionsY);
		Settings.MeshPartitionDefinition = MeshPartitionDefinitionPath.IsValid()
			? MeshPartitionDefinitionPath.TryLoad()
			: nullptr;
		return Settings;
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("CodenameGaea"), LOCTEXT("CodenameGaeaSection", "EONFORM"));
		Section.AddMenuEntry(
			TEXT("OpenCodenameGaea"),
			LOCTEXT("OpenCodenameGaeaLabel", "EONFORM"),
			LOCTEXT("OpenCodenameGaeaTooltip", "Open the EONFORM terrain inspector and output settings."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FCodenameGaeaEditorModule::OpenCodenameGaeaTab)));
	}

	void OpenCodenameGaeaTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(CodenameGaeaTabName);
	}

	void BuildPreviewMesh()
	{
		FGaeaTerrainDatasetSnapshot Snapshot;
		if (!GetLatestEvaluatedTerrain(Snapshot))
		{
			return;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoEditorWorld", "The editor world is not available."));
			return;
		}

		AGaeaTerrainDynamicMeshActor* MeshActor = nullptr;
		for (TActorIterator<AGaeaTerrainDynamicMeshActor> It(World); It; ++It)
		{
			MeshActor = *It;
			break;
		}

		if (!MeshActor)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			MeshActor = World->SpawnActor<AGaeaTerrainDynamicMeshActor>(
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				SpawnParameters);
		}

		if (!MeshActor)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GaeaMeshSpawnFailed", "Could not create the EONFORM Dynamic Mesh preview actor."));
			return;
		}

#if WITH_EDITOR
		MeshActor->SetActorLabel(TEXT("EONFORM Terrain Preview"));
#endif

		FString Error;
		if (!MeshActor->ApplyTerrainDataset(Snapshot.Dataset, MakePreviewOptions(Snapshot.Metadata.HeightScale), &Error))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::FromString(FString::Printf(TEXT("Terrain preview build failed: %s"), *Error)));
			return;
		}

		MeshActor->Modify();
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(MeshActor, true, true, true, true);
	}

	void BuildMeshTerrain()
	{
		FGaeaTerrainDatasetSnapshot Snapshot;
		if (!GetLatestEvaluatedTerrain(Snapshot))
		{
			return;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoMeshTerrainWorld", "The editor world is not available."));
			return;
		}

		const FGaeaMeshTerrainBuildResult BuildResult = FGaeaMeshTerrainOutput::Build(
			World,
			Snapshot.Dataset,
			Snapshot.Metadata.HeightScale,
			MakeMeshTerrainSettings());

		if (!BuildResult.bSuccess)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(BuildResult.Message));
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("EONFORM: %s"), *BuildResult.Message);
		if (BuildResult.TerrainActor && GEditor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(BuildResult.TerrainActor.Get(), true, true, true, true);
		}
	}

	void EditMeshPartitionDefinition()
	{
		if (!MeshPartitionDefinitionPath.IsValid() || !GEditor)
		{
			return;
		}

		if (UObject* Asset = MeshPartitionDefinitionPath.TryLoad())
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSubsystem->OpenEditorForAsset(Asset);
			}
		}
	}

	TSharedRef<SWidget> MakeOutputSettingsPanel()
	{
		return SNew(SBorder)
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MeshTerrainSettingsTitle", "Mesh Terrain Output"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("MPDLabel", "Mesh Partition Definition"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(FGaeaMeshTerrainOutput::GetMeshPartitionDefinitionClass())
						.ObjectPath_Lambda([this]() { return MeshPartitionDefinitionPath.ToString(); })
						.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
						{
							MeshPartitionDefinitionPath = AssetData.IsValid() ? AssetData.GetSoftObjectPath() : FSoftObjectPath();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("EditMPDLabel", "Edit MPD"))
						.OnClicked_Lambda([this]()
						{
							EditMeshPartitionDefinition();
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SNumericEntryBox<double>)
						.LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("XYScaleLabel", "XY Scale"))]
						.Value_Lambda([this]() { return TerrainHorizontalScale; })
						.MinValue(0.001)
						.OnValueChanged_Lambda([this](double Value) { TerrainHorizontalScale = FMath::Max(Value, 0.001); })
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SNumericEntryBox<double>)
						.LabelVAlign(VAlign_Center)
						.Label()[SNew(STextBlock).Text(LOCTEXT("ZScaleLabel", "Z Scale"))]
						.Value_Lambda([this]() { return TerrainVerticalScale; })
						.MinValue(0.001)
						.OnValueChanged_Lambda([this](double Value) { TerrainVerticalScale = FMath::Max(Value, 0.001); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResolutionPresetLabel", "Output Resolution"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SComboBox<TSharedPtr<int32>>)
						.OptionsSource(&TerrainResolutionPresets)
						.InitiallySelectedItem(FindResolutionPreset(TerrainResolutionPreset))
						.ToolTipText(LOCTEXT("ResolutionPresetTooltip", "Use the EONFORM source resolution, or resample to one of Epic's recommended UE Landscape-compatible square dimensions. Mesh Terrain itself can use arbitrary mesh resolution."))
						.OnGenerateWidget_Lambda([this](TSharedPtr<int32> Item)
						{
							return SNew(STextBlock)
								.Text(GetResolutionPresetLabel(Item.IsValid() ? *Item : 0));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Item, ESelectInfo::Type)
						{
							if (Item.IsValid())
							{
								TerrainResolutionPreset = *Item;
							}
						})
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return GetResolutionPresetLabel(TerrainResolutionPreset); })
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SNumericEntryBox<int32>)
						.Label()[SNew(STextBlock).Text(LOCTEXT("SectionsXLabel", "Sections X"))]
						.Value_Lambda([this]() { return TerrainSectionsX; })
						.MinValue(1)
						.OnValueChanged_Lambda([this](int32 Value) { TerrainSectionsX = FMath::Max(Value, 1); })
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SNumericEntryBox<int32>)
						.Label()[SNew(STextBlock).Text(LOCTEXT("SectionsYLabel", "Sections Y"))]
						.Value_Lambda([this]() { return TerrainSectionsY; })
						.MinValue(1)
						.OnValueChanged_Lambda([this](int32 Value) { TerrainSectionsY = FMath::Max(Value, 1); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputSettingsHint", "1.0 scale = authored 1:1 size. Resolution presets use Epic's recommended Landscape-compatible dimensions; Native preserves EONFORM/Mesh Terrain's source resolution. Section counts control EONFORM base regions."))
					.AutoWrapText(true)
				]
			];
	}

	TSharedRef<SDockTab> SpawnCodenameGaeaTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 6.0f, 6.0f, 0.0f)
				[
					SNew(SGaeaTerrainInspector)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.42f)
				.VAlign(VAlign_Bottom)
				.Padding(6.0f, 6.0f, 10.0f, 10.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						MakeOutputSettingsPanel()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("PreviewTerrainLabel", "Preview Mesh"))
							.ToolTipText(LOCTEXT("PreviewTerrainTooltip", "Build or refresh the lightweight Dynamic Mesh preview using the current EONFORM scale and resolution settings."))
							.OnClicked_Lambda([this]()
							{
								BuildPreviewMesh();
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("GenerateTerrainLabel", "Generate Terrain"))
							.ToolTipText(LOCTEXT("GenerateTerrainTooltip", "Build or refresh the committed UE 5.8 Mesh Terrain using the current output and streaming settings."))
							.OnClicked_Lambda([this]()
							{
								BuildMeshTerrain();
								return FReply::Handled();
							})
						]
					]
				]
			];
	}

	TSharedPtr<FGaeaTerrainGraphNodeFactory> TerrainGraphNodeFactory;
	TSharedPtr<FGaeaTerrainGraphPinFactory> TerrainGraphPinFactory;

	FSoftObjectPath MeshPartitionDefinitionPath;
	double TerrainHorizontalScale = 1.0;
	double TerrainVerticalScale = 1.0;
	int32 TerrainResolutionPreset = 0;
	int32 TerrainSectionsX = 1;
	int32 TerrainSectionsY = 1;
	TArray<TSharedPtr<int32>> TerrainResolutionPresets;
};

IMPLEMENT_MODULE(FCodenameGaeaEditorModule, CodenameGaeaEditor)

#undef LOCTEXT_NAMESPACE
