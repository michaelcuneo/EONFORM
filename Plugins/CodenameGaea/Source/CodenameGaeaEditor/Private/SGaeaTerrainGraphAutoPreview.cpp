#include "SGaeaTerrainGraphPanel.h"

#include "EdGraph/EdGraphPin.h"
#include "Misc/Crc.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	void AppendSortedMapEntries(const TMap<FName, double>& Map, TArray<FString>& Parts)
	{
		for (const TPair<FName, double>& Pair : Map) Parts.Add(FString::Printf(TEXT("N:%s=%.17g"), *Pair.Key.ToString(), Pair.Value));
	}

	void AppendSortedMapEntries(const TMap<FName, int64>& Map, TArray<FString>& Parts)
	{
		for (const TPair<FName, int64>& Pair : Map) Parts.Add(FString::Printf(TEXT("I:%s=%lld"), *Pair.Key.ToString(), static_cast<long long>(Pair.Value)));
	}

	void AppendSortedMapEntries(const TMap<FName, bool>& Map, TArray<FString>& Parts)
	{
		for (const TPair<FName, bool>& Pair : Map) Parts.Add(FString::Printf(TEXT("B:%s=%d"), *Pair.Key.ToString(), Pair.Value ? 1 : 0));
	}

	void AppendSortedMapEntries(const TMap<FName, FName>& Map, TArray<FString>& Parts)
	{
		for (const TPair<FName, FName>& Pair : Map) Parts.Add(FString::Printf(TEXT("S:%s=%s"), *Pair.Key.ToString(), *Pair.Value.ToString()));
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

	TArray<FString> Parts;
	for (UEdGraphNode* BaseNode : EditorGraph->Nodes)
	{
		const UGaeaEditorGraphNode* Node = Cast<UGaeaEditorGraphNode>(BaseNode);
		if (!Node) continue;

		Parts.Add(FString::Printf(TEXT("NODE:%s:%s"), *Node->RecipeNodeId.ToString(EGuidFormats::DigitsWithHyphens), *Node->RecipeNodeType.ToString()));
		AppendSortedMapEntries(Node->NumericParameters, Parts);
		AppendSortedMapEntries(Node->IntegerParameters, Parts);
		AppendSortedMapEntries(Node->BoolParameters, Parts);
		AppendSortedMapEntries(Node->NameParameters, Parts);

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UGaeaEditorGraphNode* LinkedNode = LinkedPin ? Cast<UGaeaEditorGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
				if (!LinkedNode) continue;
				Parts.Add(FString::Printf(
					TEXT("LINK:%s:%s>%s:%s"),
					*Node->RecipeNodeId.ToString(EGuidFormats::DigitsWithHyphens),
					*Pin->PinName.ToString(),
					*LinkedNode->RecipeNodeId.ToString(EGuidFormats::DigitsWithHyphens),
					*LinkedPin->PinName.ToString()));
			}
		}
	}

	Parts.Sort();
	FString Canonical;
	for (const FString& Part : Parts)
	{
		Canonical += Part;
		Canonical += TEXT("\n");
	}
	return FCrc::StrCrc32(*Canonical);
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

	AutoPreviewPollAccumulator += InDeltaTime;
	if (AutoPreviewPollAccumulator < 0.20f || bAutoPreviewEvaluating) return;
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
			EvaluateSelectedNodePreview();
			bAutoPreviewEvaluating = false;
		}
		return;
	}

	if (CurrentHash != LastAutoPreviewHash)
	{
		LastAutoPreviewHash = CurrentHash;
		bAutoPreviewEvaluating = true;
		EvaluateGraph();
		EvaluateSelectedNodePreview();
		bAutoPreviewEvaluating = false;
		LastPreviewNodeId = CurrentPreviewNodeId;
		return;
	}

	if (CurrentPreviewNodeId != LastPreviewNodeId)
	{
		LastPreviewNodeId = CurrentPreviewNodeId;
		bAutoPreviewEvaluating = true;
		EvaluateSelectedNodePreview();
		bAutoPreviewEvaluating = false;
	}
}
