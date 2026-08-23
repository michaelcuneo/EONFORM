#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphNode.h"

class SGraphPin;
class UEdGraphNode;
class UEdGraphPin;
enum class EGaeaEditorGraphActivity : uint8;

/**
 * Explicit visual node for Codename Gaea terrain nodes.
 *
 * Using a dedicated SGraphNode keeps input/output pin placement and pin drag
 * behavior under our control instead of relying on GraphEditor's fallback
 * widget for an otherwise unknown UEdGraphNode subclass.
 */
class SGaeaTerrainGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual TSharedPtr<SGraphPin> CreatePinWidget(UEdGraphPin* Pin) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FSlateColor GetSolveGlowColor() const;
	FSlateColor GetSolveStatusColor() const;
	FText GetSolveStatusText() const;
	EGaeaEditorGraphActivity LastObservedActivity;
};

class FGaeaTerrainGraphNodeFactory final : public FGraphPanelNodeFactory
{
public:
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override;
};
