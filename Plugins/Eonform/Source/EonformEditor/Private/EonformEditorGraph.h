#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "EonformTerrainRecipe.h"
#include "EonformEditorGraph.generated.h"

namespace EonformEditorGraphPins
{
	extern const FName Terrain;
	extern const FName ScalarField;
	extern const FName Color;
	extern const FName Any;
}

namespace EonformEditorNodeTypes
{
	extern const FName TerrainOutput;
}

UENUM()
enum class EEonformEditorGraphActivity : uint8
{
	Idle,
	Solving,
	Analyzing
};

UCLASS()
class UEonformEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UEonformEditorGraph(const FObjectInitializer& ObjectInitializer);

	void SetActivity(EEonformEditorGraphActivity InActivity);
	EEonformEditorGraphActivity GetActivity() const { return Activity; }
	bool IsBusy() const { return Activity != EEonformEditorGraphActivity::Idle; }

private:
	/** Editor-only transient worker state used purely for graph activity feedback. */
	UPROPERTY(Transient)
	EEonformEditorGraphActivity Activity = EEonformEditorGraphActivity::Idle;
};

UCLASS()
class UEonformEditorGraphNode : public UEdGraphNode
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

	UPROPERTY()
	TMap<FName, FLinearColor> ColorParameters;

	void Initialize(const FGuid& InRecipeNodeId, FName InRecipeNodeType);
	void InitializeParameterDefaults();

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override;
	virtual bool CanDuplicateNode() const override
	{
		return RecipeNodeType != EonformTerrainNodeTypes::SourceDataset
			&& RecipeNodeType != EonformEditorNodeTypes::TerrainOutput;
	}
	virtual void PrepareForCopying() override;
	virtual void PostPasteNode() override;
};

struct FEonformGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	FName NodeType = NAME_None;

	FEonformGraphSchemaAction_NewNode() = default;
	FEonformGraphSchemaAction_NewNode(
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
class UEonformEditorGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	virtual const FPinConnectionResponse CanCreateConnection(
		const UEdGraphPin* A,
		const UEdGraphPin* B) const override;

	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
};