#include "Modules/ModuleManager.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraphUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EonformEditorStyle.h"
#include "EonformTerrainGraphAsset.h"
#include "EonformTerrainGraphNode.h"
#include "EonformTerrainGraphPin.h"
#include "EonformTerrainGraphSelectionState.h"
#include "SEonformTerrainInspector.h"
#include "SEonformTerrainOutputPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FEonformEditorModule"

namespace
{
	const FName EonformTabName(TEXT("Eonform"));

	TArray<FAssetData> GetTerrainGraphAssets()
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByClass(
			UEonformTerrainGraphAsset::StaticClass()->GetClassPathName(),
			Assets,
			true);
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});
		return Assets;
	}
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

	TSharedRef<SWidget> BuildTerrainGraphMenu()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		const TArray<FAssetData> Assets = GetTerrainGraphAssets();
		if (Assets.IsEmpty())
		{
			MenuBuilder.AddWidget(
				SNew(STextBlock).Text(LOCTEXT("NoTerrainGraphs", "No terrain graph assets found")),
				FText::GetEmpty());
			return MenuBuilder.MakeWidget();
		}

		for (const FAssetData& AssetData : Assets)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromName(AssetData.AssetName),
				FText::FromName(AssetData.PackageName),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([AssetData]()
				{
					if (UEonformTerrainGraphAsset* Asset = Cast<UEonformTerrainGraphAsset>(AssetData.GetAsset()))
					{
						FEonformTerrainGraphSelectionState::Get().SetSelected(Asset);
					}
				})));
		}
		return MenuBuilder.MakeWidget();
	}

	FText GetTerrainGraphPickerText() const
	{
		if (UEonformTerrainGraphAsset* Asset = FEonformTerrainGraphSelectionState::Get().GetSelected())
		{
			return FText::FromString(Asset->GetName());
		}
		return LOCTEXT("ChooseTerrainGraph", "Choose Terrain Graph");
	}

	void InitializeTerrainGraphSelection()
	{
		if (FEonformTerrainGraphSelectionState::Get().GetSelected()) return;
		const TArray<FAssetData> Assets = GetTerrainGraphAssets();
		for (int32 Index = Assets.Num() - 1; Index >= 0; --Index)
		{
			if (UEonformTerrainGraphAsset* Asset = Cast<UEonformTerrainGraphAsset>(Assets[Index].GetAsset()))
			{
				FEonformTerrainGraphSelectionState::Get().SetSelected(Asset);
				return;
			}
		}
	}

	TSharedRef<SDockTab> SpawnEonformTab(const FSpawnTabArgs& Args)
	{
		InitializeTerrainGraphSelection();

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
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10.0f, 6.0f, 10.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TerrainGraphPickerLabel", "Terrain Graph"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
						.OnGetMenuContent_Lambda([this]() { return BuildTerrainGraphMenu(); })
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return GetTerrainGraphPickerText(); })
						]
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SEonformTerrainInspector)
					.OutputPanel(OutputPanel)
				]
			];
	}

	TSharedPtr<FEonformTerrainGraphNodeFactory> TerrainGraphNodeFactory;
	TSharedPtr<FEonformTerrainGraphPinFactory> TerrainGraphPinFactory;
};

IMPLEMENT_MODULE(FEonformEditorModule, EonformEditor)

#undef LOCTEXT_NAMESPACE
