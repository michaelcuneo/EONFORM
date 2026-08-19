#include "Modules/ModuleManager.h"

#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDynamicMeshActor.h"
#include "GaeaTerrainGraphNode.h"
#include "Misc/MessageDialog.h"
#include "SGaeaTerrainInspector.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

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
		Section.AddMenuEntry(
			TEXT("BuildCodenameGaeaDynamicMesh"),
			LOCTEXT("BuildCodenameGaeaDynamicMeshLabel", "Build Gaea Dynamic Mesh"),
			LOCTEXT("BuildCodenameGaeaDynamicMeshTooltip", "Build or refresh a Dynamic Mesh actor from the most recently evaluated Codename Gaea graph."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FCodenameGaeaEditorModule::BuildDynamicMesh)));
	}

	void OpenCodenameGaeaTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(CodenameGaeaTabName);
	}

	void BuildDynamicMesh()
	{
		FGaeaTerrainDatasetSnapshot Snapshot;
		if (!FGaeaTerrainDatasetRegistry::Get(TEXT("CodenameGaeaGraph"), Snapshot) || !Snapshot.IsValid())
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				LOCTEXT("NoEvaluatedGaeaGraph", "No evaluated Codename Gaea graph is available. Evaluate the graph first."));
			return;
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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
			SpawnParameters.Name = TEXT("CodenameGaeaDynamicMesh");
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			MeshActor = World->SpawnActor<AGaeaTerrainDynamicMeshActor>(
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				SpawnParameters);
		}

		if (!MeshActor)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GaeaMeshSpawnFailed", "Could not create the Codename Gaea Dynamic Mesh actor."));
			return;
		}

		FString Error;
		if (!MeshActor->ApplyTerrainDataset(Snapshot.Dataset, Snapshot.Metadata.HeightScale, &Error))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::FromString(FString::Printf(TEXT("Dynamic Mesh build failed: %s"), *Error)));
			return;
		}

		MeshActor->Modify();
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(MeshActor, true, true, true, true);
	}

	TSharedRef<SDockTab> SpawnCodenameGaeaTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SGaeaTerrainInspector)
			];
	}

	TSharedPtr<FGaeaTerrainGraphNodeFactory> TerrainGraphNodeFactory;
};

IMPLEMENT_MODULE(FCodenameGaeaEditorModule, CodenameGaeaEditor)

#undef LOCTEXT_NAMESPACE
