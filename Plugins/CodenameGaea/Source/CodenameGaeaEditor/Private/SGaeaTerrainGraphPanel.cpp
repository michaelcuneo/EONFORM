#include "SGaeaTerrainGraphPanel.h"

#include "EdGraph/EdGraphPin.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GraphEditor.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

void SGaeaTerrainGraphPanel::Construct(const FArguments& InArgs)
{
	OnEvaluated = InArgs._OnEvaluated;
	BuildDefaultRecipeAndGraph();

	FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(
		this,
		&SGaeaTerrainGraphPanel::OnGraphSelectionChanged);

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
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Terrain Graph")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
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
					SAssignNew(GraphEditor, SGraphEditor)
					.GraphToEdit(EditorGraph.Get())
					.GraphEvents(GraphEvents)
					.IsEditable(true)
					.ShowGraphStateOverlay(false)
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

void SGaeaTerrainGraphPanel::BuildDefaultRecipeAndGraph()
{
	EditorGraph.Reset(NewObject<UGaeaEditorGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	EditorGraph->Schema = UGaeaEditorGraphSchema::StaticClass();

	UGaeaEditorGraphNode* SourceGraphNode = NewObject<UGaeaEditorGraphNode>(EditorGraph.Get());
	SourceGraphNode->Initialize(
		FGuid(0x10101010, 0x20202020, 0x30303030, 0x40404040),
		GaeaTerrainNodeTypes::SourceDataset);
	SourceGraphNode->NodePosX = 0;
	SourceGraphNode->NodePosY = 80;
	EditorGraph->AddNode(SourceGraphNode, false, false);
	SourceGraphNode->AllocateDefaultPins();

	UGaeaEditorGraphNode* ErosionGraphNode = NewObject<UGaeaEditorGraphNode>(EditorGraph.Get());
	ErosionGraphNode->Initialize(
		FGuid(0x50505050, 0x60606060, 0x70707070, 0x80808080),
		GaeaTerrainNodeTypes::HydraulicErosion);
	ErosionGraphNode->NodePosX = 360;
	ErosionGraphNode->NodePosY = 80;
	EditorGraph->AddNode(ErosionGraphNode, false, false);
	ErosionGraphNode->AllocateDefaultPins();

	UEdGraphPin* OutputPin = SourceGraphNode->FindPin(TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* InputPin = ErosionGraphNode->FindPin(TEXT("Terrain"), EGPD_Input);
	if (OutputPin && InputPin)
	{
		OutputPin->MakeLinkTo(InputPin);
	}

	StatusText = FText::FromString(TEXT("Ready. Right-click the graph to add nodes, then Evaluate Graph."));
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
				Parameter.Type == EGaeaTerrainParameterType::Number
				? StaticCastSharedRef<SWidget>(
					SNew(SNumericEntryBox<double>)
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
					}))
				: Parameter.Type == EGaeaTerrainParameterType::Integer
				? StaticCastSharedRef<SWidget>(
					SNew(SNumericEntryBox<int64>)
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
					}))
				: Parameter.Type == EGaeaTerrainParameterType::Boolean
				? StaticCastSharedRef<SWidget>(
					SNew(SCheckBox)
					.IsChecked_Lambda([WeakNode, ParameterName]()
					{
						if (const UGaeaEditorGraphNode* Current = WeakNode.Get())
						{
							if (const bool* Value = Current->BoolParameters.Find(ParameterName)) return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}
						return ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([WeakNode, ParameterName](ECheckBoxState State)
					{
						if (UGaeaEditorGraphNode* Current = WeakNode.Get()) Current->BoolParameters.Add(ParameterName, State == ECheckBoxState::Checked);
					}))
				: StaticCastSharedRef<SWidget>(
					SNew(SEditableTextBox)
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
						if (UGaeaEditorGraphNode* Current = WeakNode.Get()) Current->NameParameters.Add(ParameterName, FName(*Text.ToString()));
					}))
			]
		];
	}
}

FReply SGaeaTerrainGraphPanel::EvaluateGraph()
{
	FGaeaTerrainDatasetSnapshot SourceSnapshot;
	if (!FGaeaTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot))
	{
		StatusText = FText::FromString(TEXT("No LegacyTerrainGenerator dataset is available. Regenerate terrain first."));
		return FReply::Handled();
	}

	FGaeaTerrainRecipe Recipe;
	FString RecipeError;
	if (!BuildRecipeFromEditorGraph(Recipe, RecipeError))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *RecipeError));
		return FReply::Handled();
	}

	FGaeaTerrainEvaluationContext Context;
	Context.SourceDataset = SourceSnapshot.Dataset;
	Context.HeightScale = SourceSnapshot.Metadata.HeightScale;

	FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	if (!Result.bSuccess)
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Graph evaluation failed: %s"), *Result.Error));
		return FReply::Handled();
	}

	const int32 FieldCount = Result.Dataset.NumScalarFields();
	const uint32 RecipeHash = Result.RecipeHash;
	const uint64 Revision = FGaeaTerrainDatasetRegistry::Publish(
		TEXT("CodenameGaeaGraph"),
		MoveTemp(Result.Dataset),
		SourceSnapshot.Metadata);

	if (Revision == 0)
	{
		StatusText = FText::FromString(TEXT("Graph evaluation succeeded, but publishing the result failed."));
		return FReply::Handled();
	}

	StatusText = FText::FromString(FString::Printf(
		TEXT("Evaluated authored recipe %08X -> revision %llu (%d fields)."),
		RecipeHash,
		static_cast<unsigned long long>(Revision),
		FieldCount));

	OnEvaluated.ExecuteIfBound();
	return FReply::Handled();
}

FText SGaeaTerrainGraphPanel::GetStatusText() const
{
	return StatusText;
}
