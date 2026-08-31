#include "Modules/ModuleManager.h"

#include "EdGraphUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "EonformEditorStyle.h"
#include "EonformTerrainGraphNode.h"
#include "EonformTerrainGraphPin.h"
#include "SEonformTerrainInspector.h"
#include "SEonformTerrainOutputPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "FEonformEditorModule"

namespace
{
	const FName EonformTabName(TEXT("Eonform"));
}

class FEonformEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FEonformEditorStyle::Initialize();

		TerrainGraphNodeFactory = MakeShared<FEonformTerrainGraphNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(TerrainGraphNodeFactory);

		TerrainGraphPinFactory = MakeShared<FEonformTerrainGraphPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(TerrainGraphPinFactory);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			EonformTabName,
			FOnSpawnTab::CreateRaw(this, &FEonformEditorModule::SpawnEonformTab))
			.SetDisplayName(LOCTEXT("EonformTabTitle", "EONFORM"))
			.SetTooltipText(LOCTEXT("EonformTabTooltip", "Open the EONFORM terrain authoring workspace."))
			.SetIcon(FSlateIcon(FEonformEditorStyle::GetStyleSetName(), TEXT("EONFORM.Tab")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FEonformEditorModule::RegisterMenus));
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
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EonformTabName);
		}

		FEonformEditorStyle::Shutdown();
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Eonform"), LOCTEXT("EonformSection", "EONFORM"));
		Section.AddMenuEntry(
			TEXT("OpenEonform"),
			LOCTEXT("OpenEonformLabel", "EONFORM"),
			LOCTEXT("OpenEonformTooltip", "Open the EONFORM terrain authoring workspace."),
			FSlateIcon(FEonformEditorStyle::GetStyleSetName(), TEXT("EONFORM.Open")),
			FUIAction(FExecuteAction::CreateRaw(this, &FEonformEditorModule::OpenEonformTab)));
	}

	void OpenEonformTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(EonformTabName);
	}

	TSharedRef<SDockTab> SpawnEonformTab(const FSpawnTabArgs& Args)
	{
		// Terrain Output is intentionally a compact, independently scrollable panel.
		// Regional Terrain Control lives in the Inspector's main workspace beside
		// the terrain preview; do not duplicate it here.
		TSharedRef<SWidget> OutputPanel =
			SNew(SBox)
			.HeightOverride(300.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SEonformTerrainOutputPanel)
				]
			];

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SEonformTerrainInspector)
				.OutputPanel(OutputPanel)
			];
	}

	TSharedPtr<FEonformTerrainGraphNodeFactory> TerrainGraphNodeFactory;
	TSharedPtr<FEonformTerrainGraphPinFactory> TerrainGraphPinFactory;
};

IMPLEMENT_MODULE(FEonformEditorModule, EonformEditor)

#undef LOCTEXT_NAMESPACE
