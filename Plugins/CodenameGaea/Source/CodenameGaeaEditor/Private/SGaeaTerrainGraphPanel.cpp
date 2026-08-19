#include "SGaeaTerrainGraphPanel.h"

#include "EdGraph/EdGraphPin.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainEvaluator.h"
#include "GraphEditor.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

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
				SAssignNew(GraphEditor, SGraphEditor)
				.GraphToEdit(EditorGraph.Get())
				.IsEditable(false)
				.ShowGraphStateOverlay(false)
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
}

void SGaeaTerrainGraphPanel::BuildDefaultRecipeAndGraph()
{
	Recipe = FGaeaTerrainRecipe();

	FGaeaTerrainNode SourceNode;
	SourceNode.Id = FGuid(0x10101010, 0x20202020, 0x30303030, 0x40404040);
	SourceNode.Type = GaeaTerrainNodeTypes::SourceDataset;

	FGaeaTerrainNode ErosionNode;
	ErosionNode.Id = FGuid(0x50505050, 0x60606060, 0x70707070, 0x80808080);
	ErosionNode.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	ErosionNode.IntegerParameters.Add(TEXT("Iterations"), 24);
	ErosionNode.NumericParameters.Add(TEXT("Rainfall"), 0.01);
	ErosionNode.NumericParameters.Add(TEXT("FlowRate"), 0.55);
	ErosionNode.NumericParameters.Add(TEXT("SedimentCapacity"), 0.7);
	ErosionNode.NumericParameters.Add(TEXT("ErosionRate"), 0.18);
	ErosionNode.NumericParameters.Add(TEXT("DepositionRate"), 0.12);
	ErosionNode.NumericParameters.Add(TEXT("Evaporation"), 0.08);
	ErosionNode.NumericParameters.Add(TEXT("MinimumSlope"), 0.01);

	Recipe.Nodes.Add(SourceNode);
	Recipe.Nodes.Add(ErosionNode);

	FGaeaTerrainConnection Connection;
	Connection.FromNode = SourceNode.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = ErosionNode.Id;
	Connection.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = ErosionNode.Id;

	EditorGraph.Reset(NewObject<UGaeaEditorGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	EditorGraph->Schema = UGaeaEditorGraphSchema::StaticClass();

	UGaeaEditorGraphNode* SourceGraphNode = NewObject<UGaeaEditorGraphNode>(EditorGraph.Get());
	SourceGraphNode->Initialize(SourceNode.Id, SourceNode.Type);
	SourceGraphNode->NodePosX = 0;
	SourceGraphNode->NodePosY = 80;
	EditorGraph->AddNode(SourceGraphNode, false, false);
	SourceGraphNode->AllocateDefaultPins();

	UGaeaEditorGraphNode* ErosionGraphNode = NewObject<UGaeaEditorGraphNode>(EditorGraph.Get());
	ErosionGraphNode->Initialize(ErosionNode.Id, ErosionNode.Type);
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

	StatusText = FText::FromString(TEXT("Ready. Uses the latest LegacyTerrainGenerator dataset as the runtime graph source."));
}

FReply SGaeaTerrainGraphPanel::EvaluateGraph()
{
	FGaeaTerrainDatasetSnapshot SourceSnapshot;
	if (!FGaeaTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot))
	{
		StatusText = FText::FromString(TEXT("No LegacyTerrainGenerator dataset is available. Regenerate terrain first."));
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
		TEXT("Evaluated recipe %08X -> revision %llu (%d fields)."),
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
