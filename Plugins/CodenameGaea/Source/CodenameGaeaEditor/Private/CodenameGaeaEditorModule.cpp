#include "Modules/ModuleManager.h"

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
#include "SGaeaTerrainInspector.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
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
		TerrainGraphNodeFactory = MakeShared<FGaeaTerrainGraphNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(TerrainGraphNodeFactory);

		TerrainGraphPinFactory = MakeShared<FGaeaTerrainGraphPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(TerrainGraphPinFactory);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			CodenameGaeaTabName,
			FOnSpawnTab::CreateRaw(this, &FCodenameGaeaEditorModule::SpawnCodenameGaeaTab))
			.SetDisplayName(LOCTEXT("CodenameGaeaTabTitle", "Codename Gaea"))
			.SetTooltipText(LOCTEXT("CodenameGaeaTabTooltip", "Open the Codename Gaea terrain dataset inspector."))
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
	}

private:
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

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("CodenameGaea"), LOCTEXT("CodenameGaeaSection", "Codename Gaea"));
		Section.AddMenuEntry(
			TEXT("OpenCodenameGaea"),
			LOCTEXT("OpenCodenameGaeaLabel", "Codename Gaea"),
			LOCTEXT("OpenCodenameGaeaTooltip", "Open the Codename Gaea terrain dataset inspector."),
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
		if (!MeshActor->ApplyTerrainDataset(Snapshot.Dataset, Snapshot.Metadata.HeightScale, &Error))
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
			Snapshot.Metadata.HeightScale);

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

	TSharedRef<SDockTab> SpawnCodenameGaeaTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10.0f, 8.0f, 10.0f, 0.0f)
				[
					SNew(SBorder)
					.Padding(FMargin(8.0f, 6.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("TerrainOutputLabel", "Terrain Output"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("PreviewTerrainLabel", "Preview Mesh"))
							.ToolTipText(LOCTEXT("PreviewTerrainTooltip", "Build or refresh the lightweight Dynamic Mesh preview from the latest evaluated EONFORM graph."))
							.OnClicked_Lambda([this]()
							{
								BuildPreviewMesh();
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("GenerateTerrainLabel", "Generate Terrain"))
							.ToolTipText(LOCTEXT("GenerateTerrainTooltip", "Build or refresh the committed UE 5.8 Mesh Terrain from the latest evaluated EONFORM graph."))
							.OnClicked_Lambda([this]()
							{
								BuildMeshTerrain();
								return FReply::Handled();
							})
						]
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SGaeaTerrainInspector)
				]
			];
	}

	TSharedPtr<FGaeaTerrainGraphNodeFactory> TerrainGraphNodeFactory;
	TSharedPtr<FGaeaTerrainGraphPinFactory> TerrainGraphPinFactory;
};

IMPLEMENT_MODULE(FCodenameGaeaEditorModule, CodenameGaeaEditor)

#undef LOCTEXT_NAMESPACE
