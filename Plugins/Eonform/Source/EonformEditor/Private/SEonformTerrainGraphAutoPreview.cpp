#include "SEonformTerrainGraphPanel.h"

#include "EdGraph/EdGraphPin.h"
#include "EonformTerrainOutputEditorState.h"
#include "EonformTerrainPhysicalMetrics.h"
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

uint32 SEonformTerrainGraphPanel::ComputeAutoPreviewHash() const
{
	if (!EditorGraph.IsValid()) return 0;

	UEonformEditorGraphNode* TerrainOutputNode = nullptr;
	for (UEdGraphNode* BaseNode : EditorGraph->Nodes)
	{
		UEonformEditorGraphNode* Node = Cast<UEonformEditorGraphNode>(BaseNode);
		if (Node && Node->RecipeNodeType == EonformEditorNodeTypes::TerrainOutput)
		{
			TerrainOutputNode = Node;
			break;
		}
	}

	TSet<FGuid> RelevantIds;
	TArray<const UEonformEditorGraphNode*> RelevantNodes;
	TFunction<void(const UEonformEditorGraphNode*)> GatherUpstream;
	GatherUpstream = [&RelevantIds, &RelevantNodes, &GatherUpstream](const UEonformEditorGraphNode* Node)
	{
		if (!Node || Node->RecipeNodeType == EonformEditorNodeTypes::TerrainOutput || RelevantIds.Contains(Node->RecipeNodeId)) return;
		RelevantIds.Add(Node->RecipeNodeId);
		RelevantNodes.Add(Node);

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEonformEditorGraphNode* Upstream = LinkedPin ? Cast<UEonformEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
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
				GatherUpstream(LinkedPin ? Cast<UEonformEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr);
			}
		}
	}

	RelevantNodes.Sort([](const UEonformEditorGraphNode& A, const UEonformEditorGraphNode& B)
	{
		return A.RecipeNodeId < B.RecipeNodeId;
	});

	uint32 Hash = 0x6f6e666du;
	for (const UEonformEditorGraphNode* Node : RelevantNodes)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Node->RecipeNodeId));
		Hash = HashCombineFast(Hash, GetTypeHash(Node->RecipeNodeType));
		HashSortedMapEntries(Hash, Node->NumericParameters);
		HashSortedMapEntries(Hash, Node->IntegerParameters);
		HashSortedMapEntries(Hash, Node->BoolParameters);
		HashSortedMapEntries(Hash, Node->NameParameters);
	}

	TArray<FSemanticGraphLink> Links;
	for (const UEonformEditorGraphNode* Node : RelevantNodes)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEonformEditorGraphNode* LinkedNode = LinkedPin ? Cast<UEonformEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
				if (!LinkedNode) continue;
				if (LinkedNode->RecipeNodeType != EonformEditorNodeTypes::TerrainOutput && !RelevantIds.Contains(LinkedNode->RecipeNodeId)) continue;

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

	const FEonformTerrainPhysicalMetrics Physical = FEonformTerrainPhysicalContext::GetActive();
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.WorldWidthMeters));
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.WorldDepthMeters));
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.ElevationScaleMeters));
	Hash = HashCombineFast(Hash, GetTypeHash(Physical.SeaLevelMeters));

	// Resolution changes alter the actual source/simulation working grid, not just
	// the final Mesh Terrain resample. They therefore must invalidate auto-preview.
	const FEonformTerrainGraphOutputSettings& OutputSettings = FEonformTerrainOutputEditorState::Get().GetSettings();
	Hash = HashCombineFast(Hash, GetTypeHash(OutputSettings.OutputResolution));
	return Hash;
}

void SEonformTerrainGraphPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SyncOutputSettingsState();

	if (!bLegacyEvaluateButtonHidden)
	{
		bLegacyEvaluateButtonHidden = HideButtonWithText(SharedThis(this), TEXT("Evaluate Graph"));
	}

	AutoPreviewPollAccumulator += InDeltaTime;
	if (AutoPreviewPollAccumulator < 0.35f) return;
	AutoPreviewPollAccumulator = 0.0f;

	const uint32 CurrentHash = ComputeAutoPreviewHash();
	const UEonformEditorGraphNode* CurrentPreviewNode = SelectedNode.Get();
	const FGuid CurrentPreviewNodeId = CurrentPreviewNode ? CurrentPreviewNode->RecipeNodeId : FGuid();

	if (!bAutoPreviewInitialized)
	{
		LastAutoPreviewHash = CurrentHash;
		LastPreviewNodeId = CurrentPreviewNodeId;
		bAutoPreviewInitialized = true;
		RequestFinalEvaluationAsync();
		return;
	}

	if (CurrentHash != LastAutoPreviewHash)
	{
		LastAutoPreviewHash = CurrentHash;
		LastPreviewNodeId = CurrentPreviewNodeId;
		RequestFinalEvaluationAsync();
		return;
	}

	// Selection and layout are editor-only state. Never launch a second terrain solve
	// just because the user clicked or dragged a node. Intermediate-node inspection can
	// be layered back onto the async queue later without touching final Terrain Output.
	if (CurrentPreviewNodeId.IsValid()) LastPreviewNodeId = CurrentPreviewNodeId;
}
