#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "GaeaEditorGraph.generated.h"

UCLASS()
class UGaeaEditorGraph : public UEdGraph
{
	GENERATED_BODY()
};

UCLASS()
class UGaeaEditorGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid RecipeNodeId;

	UPROPERTY()
	FName RecipeNodeType = NAME_None;

	void Initialize(const FGuid& InRecipeNodeId, FName InRecipeNodeType);

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
};

UCLASS()
class UGaeaEditorGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	virtual const FPinConnectionResponse CanCreateConnection(
		const UEdGraphPin* A,
		const UEdGraphPin* B) const override;

	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
};
