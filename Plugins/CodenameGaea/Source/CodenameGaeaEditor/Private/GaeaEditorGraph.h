#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "GaeaTerrainRecipe.h"
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

	UPROPERTY()
	TMap<FName, double> NumericParameters;

	UPROPERTY()
	TMap<FName, int64> IntegerParameters;

	UPROPERTY()
	TMap<FName, bool> BoolParameters;

	UPROPERTY()
	TMap<FName, FName> NameParameters;

	void Initialize(const FGuid& InRecipeNodeId, FName InRecipeNodeType);
	void InitializeParameterDefaults();

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override;
	virtual bool CanDuplicateNode() const override
	{
		return RecipeNodeType != GaeaTerrainNodeTypes::SourceDataset;
	}
	virtual void PrepareForCopying() override;
	virtual void PostPasteNode() override;
};

struct FGaeaGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	FName NodeType = NAME_None;

	FGaeaGraphSchemaAction_NewNode() = default;
	FGaeaGraphSchemaAction_NewNode(
		const FText& InNodeCategory,
		const FText& InMenuDesc,
		const FText& InToolTip,
		FName InNodeType);

	virtual UEdGraphNode* PerformAction(
		UEdGraph* ParentGraph,
		UEdGraphPin* FromPin,
		const FVector2D Location,
		bool bSelectNewNode = true) override;
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
