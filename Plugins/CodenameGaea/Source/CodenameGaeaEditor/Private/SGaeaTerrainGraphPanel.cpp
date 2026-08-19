#include "SGaeaTerrainGraphPanel.h"

#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "EdGraph/EdGraphPin.h"
#include "FileHelpers.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainGraphAsset.h"
#include "GaeaTerrainGraphAssetFactory.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	UGaeaTerrainGraphAsset* CreateTerrainGraphAssetWithDialog()
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UGaeaTerrainGraphAssetFactory* Factory = NewObject<UGaeaTerrainGraphAssetFactory>();
		return Cast<UGaeaTerrainGraphAsset>(AssetToolsModule.Get().CreateAssetWithDialog(
			UGaeaTerrainGraphAsset::StaticClass(),
			Factory,
			TEXT("CodenameGaea")));
	}

	void ApplyDescriptorDefaults(FGaeaTerrainNode& Node)
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		if (!FGaeaTerrainNodeDescriptorRegistry::Get(Node.Type, Descriptor))
		{
			return;
		}

		for (const FGaeaTerrainParameterDescriptor& Parameter : Descriptor.Parameters)
		{
			switch (Parameter.Type)
			{
			case EGaeaTerrainParameterType::Number:
				Node.NumericParameters.Add(Parameter.Name, Parameter.DefaultNumber);
				break;
			case EGaeaTerrainParameterType::Integer:
				Node.IntegerParameters.Add(Parameter.Name, Parameter.DefaultInteger);
				break;
			case EGaeaTerrainParameterType::Boolean:
				Node.BoolParameters.Add(Parameter.Name, Parameter.DefaultBoolean);
				break;
			case EGaeaTerrainParameterType::Name:
				Node.NameParameters.Add(Parameter.Name, Parameter.DefaultName);
				break;
			}
		}
	}
}

void SGaeaTerrainGraphPanel::Construct(const FArguments& InArgs)
{
	OnEvaluated = InArgs._OnEvaluated;
	BuildDefaultRecipeAndGraph();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(6.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Terrain Graph")))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SGaeaTerrainGraphPanel::GetAssetText)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("New")))
					.OnClicked(this, &SGaeaTerrainGraphPanel::NewGraphAsset)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Open")))
					.OnClicked(this, &SGaeaTerrainGraphPanel::OpenGraphAsset)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Save")))
					.OnClicked(this, &SGaeaTerrainGraphPanel::SaveGraphAsset)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Evaluate Graph")))
					.OnClicked(this, &SGaeaTerrainGraphPanel::EvaluateGraph)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(0.78f)
				[
					SAssignNew(GraphHost, SBox)
					[
						CreateGraphEditorWidget()
					]
				]
				+ SSplitter::Slot()
				.Value(0.22f)
				[
					SNew(SBorder)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SAssignNew(ParameterPanel, SVerticalBox)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SGaeaTerrainGraphPanel::GetStatusText)
			]
		]
	];

	RebuildParameterPanel();
}

TSharedRef<SWidget> SGaeaTerrainGraphPanel::CreateGraphEditorWidget()
{
	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(
		this,
		&SGaeaTerrainGraphPanel::OnGraphSelectionChanged);

	return SAssignNew(GraphEditor, SGraphEditor)
		.GraphToEdit(EditorGraph.Get())
		.GraphEvents(GraphEvents)
		.IsEditable(true)
		.ShowGraphStateOverlay(false);
}

void SGaeaTerrainGraphPanel::BuildDefaultRecipeAndGraph()
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode SourceNode;
	SourceNode.Id = FGuid(0x10101010, 0x20202020, 0x30303030, 0x40404040);
	SourceNode.Type = GaeaTerrainNodeTypes::ProceduralTerrain;
	ApplyDescriptorDefaults(SourceNode);

	FGaeaTerrainNode ErosionNode;
	ErosionNode.Id = FGuid(0x50505050, 0x60606060, 0x70707070, 0x80808080);
	ErosionNode.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	ApplyDescriptorDefaults(ErosionNode);

	Recipe.Nodes.Add(SourceNode);
	Recipe.Nodes.Add(ErosionNode);

	FGaeaTerrainConnection Connection;
	Connection.FromNode = SourceNode.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = ErosionNode.Id;
	Connection.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = ErosionNode.Id;

	BuildEditorGraphFromRecipe(Recipe, nullptr);
	StatusText = FText::FromString(TEXT("Unsaved procedural graph. Adjust parameters, Evaluate Graph, then Generate Dynamic Mesh."));
}

void SGaeaTerrainGraphPanel::BuildEditorGraphFromRecipe(
	const FGaeaTerrainRecipe& Recipe,
	const UGaeaTerrainGraphAsset* Asset)
{
	SelectedNode.Reset();
	EditorGraph.Reset(NewObject<UGaeaEditorGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	EditorGraph->Schema = UGaeaEditorGraphSchema::StaticClass();

	TMap<FGuid, UGaeaEditorGraphNode*> NodeMap;
	int32 DefaultIndex = 0;

	for (const FGaeaTerrainNode& RecipeNode : Recipe.Nodes)
	{
		UGaeaEditorGraphNode* GraphNode = NewObject<UGaeaEditorGraphNode>(EditorGraph.Get());
		GraphNode->Initialize(RecipeNode.Id, RecipeNode.Type);
		GraphNode->NumericParameters = RecipeNode.NumericParameters;
		GraphNode->IntegerParameters = RecipeNode.IntegerParameters;
		GraphNode->BoolParameters = RecipeNode.BoolParameters;
		GraphNode->NameParameters = RecipeNode.NameParameters;

		if (Asset)
		{
			if (const FGaeaTerrainNodeLayout* Layout = Asset->FindLayout(RecipeNode.Id))
			{
				GraphNode->NodePosX = FMath::RoundToInt(Layout->Position.X);
				GraphNode->NodePosY = FMath::RoundToInt(Layout->Position.Y);
			}
			else
			{
				GraphNode->NodePosX = DefaultIndex * 360;
				GraphNode->NodePosY = 80;
			}
		}
		else
		{
			GraphNode->NodePosX = DefaultIndex * 360;
			GraphNode->NodePosY = 80;
		}

		EditorGraph->AddNode(GraphNode, false, false);
		GraphNode->AllocateDefaultPins();
		NodeMap.Add(RecipeNode.Id, GraphNode);
		++DefaultIndex;
	}

	for (const FGaeaTerrainConnection& Connection : Recipe.Connections)
	{
		UGaeaEditorGraphNode* const* FromNode = NodeMap.Find(Connection.FromNode);
		UGaeaEditorGraphNode* const* ToNode = NodeMap.Find(Connection.ToNode);
		if (!FromNode || !ToNode || !*FromNode || !*ToNode)
		{
			continue;
		}

		UEdGraphPin* OutputPin = (*FromNode)->FindPin(Connection.FromOutput, EGPD_Output);
		UEdGraphPin* InputPin = (*ToNode)->FindPin(Connection.ToInput, EGPD_Input);
		if (OutputPin && InputPin)
		{
			OutputPin->MakeLinkTo(InputPin);
		}
	}

	if (GraphHost.IsValid())
	{
		GraphHost->SetContent(CreateGraphEditorWidget());
	}

	RebuildParameterPanel();
}

bool SGaeaTerrainGraphPanel::BuildRecipeFromEditorGraph(
	FGaeaTerrainRecipe& OutRecipe,
	FString& OutError) const
{
	OutRecipe = FGaeaTerrainRecipe();
	OutError.Reset();

	if (!EditorGraph.IsValid())
	{
		OutError = TEXT("Editor graph is not available.");
		return false;
	}

	TArray<UGaeaEditorGraphNode*> TerrainNodes;
	for (UEdGraphNode* Node : EditorGraph->Nodes)
	{
		if (UGaeaEditorGraphNode* TerrainNode = Cast<UGaeaEditorGraphNode>(Node))
		{
			TerrainNodes.Add(TerrainNode);

			FGaeaTerrainNode RecipeNode;
			RecipeNode.Id = TerrainNode->RecipeNodeId;
			RecipeNode.Type = TerrainNode->RecipeNodeType;
			RecipeNode.NumericParameters = TerrainNode->NumericParameters;
			RecipeNode.IntegerParameters = TerrainNode->IntegerParameters;
			RecipeNode.BoolParameters = TerrainNode->BoolParameters;
			RecipeNode.NameParameters = TerrainNode->NameParameters;
			OutRecipe.Nodes.Add(MoveTemp(RecipeNode));
		}
	}

	if (TerrainNodes.IsEmpty())
	{
		OutError = TEXT("The terrain graph contains no nodes.");
		return false;
	}

	TSet<FGuid> NodesWithOutgoingConnections;
	for (UGaeaEditorGraphNode* TerrainNode : TerrainNodes)
	{
		for (UEdGraphPin* Pin : TerrainNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				UGaeaEditorGraphNode* ToNode = LinkedPin
					? Cast<UGaeaEditorGraphNode>(LinkedPin->GetOwningNode())
					: nullptr;
				if (!ToNode)
				{
					continue;
				}

				FGaeaTerrainConnection Connection;
				Connection.FromNode = TerrainNode->RecipeNodeId;
				Connection.FromOutput = Pin->PinName;
				Connection.ToNode = ToNode->RecipeNodeId;
				Connection.ToInput = LinkedPin->PinName;
				OutRecipe.Connections.Add(MoveTemp(Connection));
				NodesWithOutgoingConnections.Add(TerrainNode->RecipeNodeId);
			}
		}
	}

	TArray<FGuid> TerminalNodes;
	for (const UGaeaEditorGraphNode* TerrainNode : TerrainNodes)
	{
		if (!NodesWithOutgoingConnections.Contains(TerrainNode->RecipeNodeId))
		{
			TerminalNodes.Add(TerrainNode->RecipeNodeId);
		}
	}

	if (TerminalNodes.Num() != 1)
	{
		OutError = FString::Printf(
			TEXT("The terrain graph must have exactly one terminal output node; found %d."),
			TerminalNodes.Num());
		return false;
	}

	OutRecipe.OutputNode = TerminalNodes[0];
	return OutRecipe.Validate(&OutError);
}

bool SGaeaTerrainGraphPanel::WriteEditorGraphToAsset(
	UGaeaTerrainGraphAsset& Asset,
	FString& OutError) const
{
	FGaeaTerrainRecipe Recipe;
	if (!BuildRecipeFromEditorGraph(Recipe, OutError))
	{
		return false;
	}

	Asset.Modify();
	Asset.Recipe = MoveTemp(Recipe);
	Asset.NodeLayout.Reset();

	for (UEdGraphNode* Node : EditorGraph->Nodes)
	{
		if (const UGaeaEditorGraphNode* TerrainNode = Cast<UGaeaEditorGraphNode>(Node))
		{
			Asset.SetLayout(
				TerrainNode->RecipeNodeId,
				FVector2D(static_cast<double>(TerrainNode->NodePosX), static_cast<double>(TerrainNode->NodePosY)));
		}
	}

	Asset.MarkPackageDirty();
	OutError.Reset();
	return true;
}

void SGaeaTerrainGraphPanel::LoadAsset(UGaeaTerrainGraphAsset& Asset)
{
	FString Error;
	if (!Asset.Recipe.Validate(&Error))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Cannot open graph asset: %s"), *Error));
		return;
	}

	CurrentAsset.Reset(&Asset);
	BuildEditorGraphFromRecipe(Asset.Recipe, &Asset);
	StatusText = FText::FromString(FString::Printf(TEXT("Opened %s."), *Asset.GetPathName()));
}

UGaeaTerrainGraphAsset* SGaeaTerrainGraphPanel::CreateAssetFromCurrentGraph()
{
	UGaeaTerrainGraphAsset* Asset = CreateTerrainGraphAssetWithDialog();
	if (!Asset)
	{
		return nullptr;
	}

	FString Error;
	if (!WriteEditorGraphToAsset(*Asset, Error))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Could not create graph asset: %s"), *Error));
		return nullptr;
	}

	CurrentAsset.Reset(Asset);
	return Asset;
}

bool SGaeaTerrainGraphPanel::SaveCurrentAsset(bool bPromptForCheckout)
{
	if (!CurrentAsset.IsValid())
	{
		return false;
	}

	FString Error;
	if (!WriteEditorGraphToAsset(*CurrentAsset.Get(), Error))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Save failed: %s"), *Error));
		return false;
	}

	UPackage* Package = CurrentAsset->GetOutermost();
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Package);
	FEditorFileUtils::PromptForCheckoutAndSave(
		PackagesToSave,
		true,
		false,
		nullptr,
		!bPromptForCheckout,
		false);

	return !Package->IsDirty();
}

FReply SGaeaTerrainGraphPanel::NewGraphAsset()
{
	UGaeaTerrainGraphAsset* NewAsset = CreateTerrainGraphAssetWithDialog();
	if (!NewAsset)
	{
		StatusText = FText::FromString(TEXT("New graph cancelled."));
		return FReply::Handled();
	}

	BuildDefaultRecipeAndGraph();
	CurrentAsset.Reset(NewAsset);

	if (SaveCurrentAsset(true))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Created %s."), *NewAsset->GetPathName()));
	}
	return FReply::Handled();
}

FReply SGaeaTerrainGraphPanel::OpenGraphAsset()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FOpenAssetDialogConfig Config;
	Config.AssetClassNames.Add(UGaeaTerrainGraphAsset::StaticClass()->GetClassPathName());
	Config.bAllowMultipleSelection = false;
	Config.DialogTitleOverride = FText::FromString(TEXT("Open Codename Gaea Graph"));

	const TArray<FAssetData> SelectedAssets = ContentBrowserModule.Get().CreateModalOpenAssetDialog(Config);
	if (SelectedAssets.IsEmpty())
	{
		StatusText = FText::FromString(TEXT("Open graph cancelled."));
		return FReply::Handled();
	}

	if (UGaeaTerrainGraphAsset* Asset = Cast<UGaeaTerrainGraphAsset>(SelectedAssets[0].GetAsset()))
	{
		LoadAsset(*Asset);
	}
	else
	{
		StatusText = FText::FromString(TEXT("The selected asset is not a Codename Gaea graph."));
	}

	return FReply::Handled();
}

FReply SGaeaTerrainGraphPanel::SaveGraphAsset()
{
	if (!CurrentAsset.IsValid())
	{
		if (!CreateAssetFromCurrentGraph())
		{
			StatusText = FText::FromString(TEXT("Save cancelled."));
			return FReply::Handled();
		}
	}

	if (SaveCurrentAsset(true))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Saved %s."), *CurrentAsset->GetPathName()));
	}
	return FReply::Handled();
}

void SGaeaTerrainGraphPanel::OnGraphSelectionChanged(const TSet<UObject*>& NewSelection)
{
	SelectedNode.Reset();
	if (NewSelection.Num() == 1)
	{
		for (UObject* Object : NewSelection)
		{
			if (UGaeaEditorGraphNode* Node = Cast<UGaeaEditorGraphNode>(Object))
			{
				SelectedNode = Node;
			}
		}
	}
	RebuildParameterPanel();
}

void SGaeaTerrainGraphPanel::RebuildParameterPanel()
{
	if (!ParameterPanel.IsValid())
	{
		return;
	}

	ParameterPanel->ClearChildren();
	UGaeaEditorGraphNode* Node = SelectedNode.Get();
	if (!Node)
	{
		ParameterPanel->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Select a graph node to edit its parameters.")))
			.AutoWrapText(true)
		];
		return;
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (!FGaeaTerrainNodeDescriptorRegistry::Get(Node->RecipeNodeType, Descriptor))
	{
		return;
	}

	ParameterPanel->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Descriptor.DisplayName))
	];

	if (Descriptor.Parameters.IsEmpty())
	{
		ParameterPanel->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("This node has no editable parameters.")))
		];
		return;
	}

	for (const FGaeaTerrainParameterDescriptor& Parameter : Descriptor.Parameters)
	{
		TWeakObjectPtr<UGaeaEditorGraphNode> WeakNode(Node);
		const FName ParameterName = Parameter.Name;
		TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;

		switch (Parameter.Type)
		{
		case EGaeaTerrainParameterType::Number:
			ValueWidget = SNew(SNumericEntryBox<double>)
				.Value_Lambda([WeakNode, ParameterName]() -> TOptional<double>
				{
					if (const UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						if (const double* Value = Current->NumericParameters.Find(ParameterName)) return *Value;
					}
					return TOptional<double>();
				})
				.MinValue(Parameter.bHasMinimum ? TOptional<double>(Parameter.Minimum) : TOptional<double>())
				.MaxValue(Parameter.bHasMaximum ? TOptional<double>(Parameter.Maximum) : TOptional<double>())
				.OnValueCommitted_Lambda([WeakNode, ParameterName](double Value, ETextCommit::Type)
				{
					if (UGaeaEditorGraphNode* Current = WeakNode.Get()) Current->NumericParameters.Add(ParameterName, Value);
				});
			break;

		case EGaeaTerrainParameterType::Integer:
			ValueWidget = SNew(SNumericEntryBox<int64>)
				.Value_Lambda([WeakNode, ParameterName]() -> TOptional<int64>
				{
					if (const UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						if (const int64* Value = Current->IntegerParameters.Find(ParameterName)) return *Value;
					}
					return TOptional<int64>();
				})
				.MinValue(Parameter.bHasMinimum ? TOptional<int64>(static_cast<int64>(Parameter.Minimum)) : TOptional<int64>())
				.MaxValue(Parameter.bHasMaximum ? TOptional<int64>(static_cast<int64>(Parameter.Maximum)) : TOptional<int64>())
				.OnValueCommitted_Lambda([WeakNode, ParameterName](int64 Value, ETextCommit::Type)
				{
					if (UGaeaEditorGraphNode* Current = WeakNode.Get()) Current->IntegerParameters.Add(ParameterName, Value);
				});
			break;

		case EGaeaTerrainParameterType::Boolean:
			ValueWidget = SNew(SCheckBox)
				.IsChecked_Lambda([WeakNode, ParameterName]()
				{
					if (const UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						if (const bool* Value = Current->BoolParameters.Find(ParameterName))
						{
							return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}
					}
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakNode, ParameterName](ECheckBoxState State)
				{
					if (UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						Current->BoolParameters.Add(ParameterName, State == ECheckBoxState::Checked);
					}
				});
			break;

		case EGaeaTerrainParameterType::Name:
			ValueWidget = SNew(SEditableTextBox)
				.Text_Lambda([WeakNode, ParameterName]()
				{
					if (const UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						if (const FName* Value = Current->NameParameters.Find(ParameterName)) return FText::FromName(*Value);
					}
					return FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([WeakNode, ParameterName](const FText& Text, ETextCommit::Type)
				{
					if (UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						Current->NameParameters.Add(ParameterName, FName(*Text.ToString()));
					}
				});
			break;
		}

		ParameterPanel->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 3.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Parameter.DisplayName))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ValueWidget
			]
		];
	}
}

FReply SGaeaTerrainGraphPanel::EvaluateGraph()
{
	FGaeaTerrainRecipe Recipe;
	FString RecipeError;
	if (!BuildRecipeFromEditorGraph(Recipe, RecipeError))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *RecipeError));
		return FReply::Handled();
	}

	const bool bUsesExternalSource = Recipe.Nodes.ContainsByPredicate([](const FGaeaTerrainNode& Node)
	{
		return Node.Type == GaeaTerrainNodeTypes::SourceDataset;
	});

	FGaeaTerrainEvaluationContext Context;
	if (bUsesExternalSource)
	{
		FGaeaTerrainDatasetSnapshot SourceSnapshot;
		if (!FGaeaTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot) || !SourceSnapshot.IsValid())
		{
			StatusText = FText::FromString(TEXT("This graph uses Source Dataset, but no LegacyTerrainGenerator dataset is available."));
			return FReply::Handled();
		}
		Context.SourceDataset = SourceSnapshot.Dataset;
		Context.HeightScale = SourceSnapshot.Metadata.HeightScale;
	}

	FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	if (!Result.bSuccess)
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Graph evaluation failed: %s"), *Result.Error));
		return FReply::Handled();
	}

	const int32 FieldCount = Result.Dataset.NumScalarFields();
	const uint32 RecipeHash = Result.RecipeHash;
	FGaeaTerrainDatasetMetadata Metadata;
	Metadata.HeightScale = Result.HeightScale;

	const uint64 Revision = FGaeaTerrainDatasetRegistry::Publish(
		TEXT("CodenameGaeaGraph"),
		MoveTemp(Result.Dataset),
		Metadata);

	if (Revision == 0)
	{
		StatusText = FText::FromString(TEXT("Graph evaluation succeeded, but publishing the result failed."));
		return FReply::Handled();
	}

	StatusText = FText::FromString(FString::Printf(
		TEXT("Evaluated authored recipe %08X -> revision %llu (%d fields, height scale %.1f)."),
		RecipeHash,
		static_cast<unsigned long long>(Revision),
		FieldCount,
		Metadata.HeightScale));

	OnEvaluated.ExecuteIfBound();
	return FReply::Handled();
}

FText SGaeaTerrainGraphPanel::GetAssetText() const
{
	if (!CurrentAsset.IsValid())
	{
		return FText::FromString(TEXT("Unsaved graph"));
	}

	return FText::FromString(FString::Printf(TEXT("Asset: %s"), *CurrentAsset->GetPathName()));
}

FText SGaeaTerrainGraphPanel::GetStatusText() const
{
	return StatusText;
}
