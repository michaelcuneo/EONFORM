#include "SGaeaTerrainGraphPanel.h"

#include "EdGraph/EdGraphPin.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	template<typename ValueType>
	void HashSortedMapEntries(uint32& Hash, const TMap<FName, ValueType>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (const FName Key : Keys)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Key));
			Hash = HashCombineFast(Hash, GetTypeHash(Map.FindChecked(Key)));
		}
	}

	struct FSemanticGraphLink
	{
		FGuid FromNode;
		FName FromPin = NAME_None;
		FGuid ToNode;
		FName ToPin = NAME_None;
	};

	bool SemanticLinkLess(const FSemanticGraphLink& A, const FSemanticGraphLink& B)
	{
		if (A.ToNode != B.ToNode) return A.ToNode < B.ToNode;
		if (A.ToPin != B.ToPin) return A.ToPin.LexicalLess(B.ToPin);
		if (A.FromNode != B.FromNode) return A.FromNode < B.FromNode;
		return A.FromPin.LexicalLess(B.FromPin);
	}

	bool WidgetTreeContainsText(const TSharedRef<SWidget>& Widget, const FString& Text)
	{
		if (Widget->GetTypeAsString() == TEXT("STextBlock"))
		{
			const TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(Widget);
			if (TextBlock->GetText().ToString() == Text) return true;
		}

		FChildren* Children = Widget->GetChildren();
		if (!Children) return false;
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (WidgetTreeContainsText(Children->GetChildAt(Index), Text)) return true;
		}
		return false;
	}

	bool HideButtonWithText(const TSharedRef<SWidget>& Widget, const FString& Text)
	{
		if (Widget->GetTypeAsString() == TEXT("SButton") && WidgetTreeContainsText(Widget, Text))
		{
			Widget->SetVisibility(EVisibility::Collapsed);
			return true;
		}

		FChildren* Children = Widget->GetChildren();
		if (!Children) return false;
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (HideButtonWithText(Children->GetChildAt(Index), Text)) return true;
		}
		return false;
	}
}

uint32 SGaeaTerrainGraphPanel::ComputeAutoPreviewHash() const
{
	if (!EditorGraph.IsValid()) return 0;

	UGaeaEditorGraphNode* TerrainOutputNode = nullptr;
	for (UEdGraphNode* BaseNode : EditorGraph->Nodes)
	{
		UGaeaEditorGraphNode* Node = Cast<UGaeaEditorGraphNode>(BaseNode);
		if (Node && Node->RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
		{
			TerrainOutputNode = Node;
			break;
		}
	}

	TSet<FGuid> RelevantIds;
	TArray<const UGaeaEditorGraphNode*> RelevantNodes;
	TFunction<void(const UGaeaEditorGraphNode*)> GatherUpstream;
	GatherUpstream = [&RelevantIds, &RelevantNodes, &GatherUpstream](const UGaeaEditorGraphNode* Node)
	{
		if (!Node || Node->RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput || RelevantIds.Contains(Node->RecipeNodeId)) return;
		RelevantIds.Add(Node->RecipeNodeId);
		RelevantNodes.Add(Node);

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UGaeaEditorGraphNode* Upstream = LinkedPin ? Cast<UGaeaEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
				GatherUpstream(Upstream);
			}
		}
	};

	if (TerrainOutputNode)
	{
		for (const UEdGraphPin* Pin : TerrainOutputNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				GatherUpstream(LinkedPin ? Cast<UGaeaEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr);
			}
		}
	}

	RelevantNodes.Sort([](const UGaeaEditorGraphNode& A, const UGaeaEditorGraphNode& B)
	{
		return A.RecipeNodeId < B.RecipeNodeId;
	});

	uint32 Hash = 0x6f6e666du; // "onfm" marker for the semantic preview hash.
	for (const UGaeaEditorGraphNode* Node : RelevantNodes)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Node->RecipeNodeId));
		Hash = HashCombineFast(Hash, GetTypeHash(Node->RecipeNodeType));
		HashSortedMapEntries(Hash, Node->NumericParameters);
		HashSortedMapEntries(Hash, Node->IntegerParameters);
		HashSortedMapEntries(Hash, Node->BoolParameters);
		HashSortedMapEntries(Hash, Node->NameParameters);
	}

	TArray<FSemanticGraphLink> Links;
	for (const UGaeaEditorGraphNode* Node : RelevantNodes)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UGaeaEditorGraphNode* LinkedNode = LinkedPin ? Cast<UGaeaEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
				if (!LinkedNode) continue;
				if (LinkedNode->RecipeNodeType != GaeaEditorNodeTypes::TerrainOutput && !RelevantIds.Contains(LinkedNode->RecipeNodeId)) continue;

				FSemanticGraphLink Link;
				Link.FromNode = Node->RecipeNodeId;
				Link.FromPin = Pin->PinName;
				Link.ToNode = LinkedNode->RecipeNodeId;
				Link.ToPin = LinkedPin->PinName;
				Links.Add(Link);
			}
		}
	}
	Links.Sort(SemanticLinkLess);
	for (const FSemanticGraphLink& Link : Links)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Link.FromNode));
		Hash = HashCombineFast(Hash, GetTypeHash(Link.FromPin));
		Hash = HashCombineFast(Hash, GetTypeHash(Link.ToNode));
		Hash = HashCombineFast(Hash, GetTypeHash(Link.ToPin));
	}

	// Physical dimensions are part of terrain evaluation now, not merely final mesh scale.
	const FGaeaTerrainPhysicalMetrics Physical = FGaeaTerrainPhysicalContext::GetActive();
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.WorldWidthMeters));
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.WorldDepthMeters));
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.ElevationScaleMeters));
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.SeaLevelMeters));
	return Hash;
}

void SGaeaTerrainGraphPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Keep the active graph asset and the Terrain Output pane synchronized. Output
	// changes mark the graph dirty and opening another graph restores its saved settings.
	SyncOutputSettingsState();

	if (!bLegacyEvaluateButtonHidden)
	{
		bLegacyEvaluateButtonHidden = HideButtonWithText(SharedThis(this), TEXT("Evaluate Graph"));
	}

	// Auto-preview is intentionally a coarse semantic poll, not a canvas-frame poll.
	// Node positions and disconnected scratch nodes are excluded from the hash, so graph
	// layout work must never launch terrain simulation.
	AutoPreviewPollAccumulator += InDeltaTime;
	if (AutoPreviewPollAccumulator < 0.75f || bAutoPreviewEvaluating) return;
	AutoPreviewPollAccumulator = 0.0f;

	const uint32 CurrentHash = ComputeAutoPreviewHash();
	const UGaeaEditorGraphNode* CurrentPreviewNode = SelectedNode.Get();
	const FGuid CurrentPreviewNodeId = CurrentPreviewNode ? CurrentPreviewNode->RecipeNodeId : FGuid();

	if (!bAutoPreviewInitialized)
	{
		LastAutoPreviewHash = CurrentHash;
		LastPreviewNodeId = CurrentPreviewNodeId;
		bAutoPreviewInitialized = true;
		if (EditorGraph.IsValid() && EditorGraph->Nodes.Num() > 1)
		{
			bAutoPreviewEvaluating = true;
			EvaluateGraph();
			bAutoPreviewEvaluating = false;
		}
		return;
	}

	if (CurrentHash != LastAutoPreviewHash)
	{
		LastAutoPreviewHash = CurrentHash;
		bAutoPreviewEvaluating = true;
		EvaluateGraph();
		bAutoPreviewEvaluating = false;

		// A semantic graph edit already refreshed the authoritative Terrain Output.
		// Do not immediately evaluate the selected node a second time. The next explicit
		// selection change can request an intermediate preview if the user wants one.
		if (CurrentPreviewNodeId.IsValid()) LastPreviewNodeId = CurrentPreviewNodeId;
		return;
	}

	// GraphEditor can transiently clear selection while a node is being dragged. Treat
	// that as canvas interaction, not a preview request. Keeping LastPreviewNodeId intact
	// prevents the same node from being reevaluated when the drag finishes.
	if (CurrentPreviewNodeId.IsValid() && CurrentPreviewNodeId != LastPreviewNodeId)
	{
		LastPreviewNodeId = CurrentPreviewNodeId;
		bAutoPreviewEvaluating = true;
		EvaluateSelectedNodePreview();
		bAutoPreviewEvaluating = false;
	}
}
