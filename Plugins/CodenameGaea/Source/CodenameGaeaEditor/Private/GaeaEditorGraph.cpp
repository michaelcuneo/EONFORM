#include "GaeaEditorGraph.h"

#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaEditorNodeTypes
{
	const FName TerrainOutput(TEXT("__TerrainOutput"));
}

namespace
{
	const FName TerrainPinCategory(TEXT("GaeaTerrain"));
	const FName TerrainPinName(TEXT("Terrain"));

	bool CanReachNode(const UEdGraphNode* StartNode, const UEdGraphNode* TargetNode)
	{
		if (!StartNode || !TargetNode)
		{
			return false;
		}

		TArray<const UEdGraphNode*> Stack;
		TSet<const UEdGraphNode*> Visited;
		Stack.Add(StartNode);

		while (!Stack.IsEmpty())
		{
			const UEdGraphNode* Current = Stack.Pop(EAllowShrinking::No);
			if (!Current || Visited.Contains(Current))
			{
				continue;
			}
			if (Current == TargetNode)
			{
				return true;
			}

			Visited.Add(Current);
			for (const UEdGraphPin* Pin : Current->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin && LinkedPin->GetOwningNode())
					{
						Stack.Add(LinkedPin->GetOwningNode());
					}
				}
			}
		}

		return false;
	}
}

void UGaeaEditorGraphNode::Initialize(const FGuid& InRecipeNodeId, FName InRecipeNodeType)
{
	RecipeNodeId = InRecipeNodeId;
	RecipeNodeType = InRecipeNodeType;
	InitializeParameterDefaults();
}

void UGaeaEditorGraphNode::InitializeParameterDefaults()
{
	NumericParameters.Reset();
	IntegerParameters.Reset();
	BoolParameters.Reset();
	NameParameters.Reset();

	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		return;
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (!FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor))
	{
		return;
	}

	for (const FGaeaTerrainParameterDescriptor& Parameter : Descriptor.Parameters)
	{
		switch (Parameter.Type)
		{
		case EGaeaTerrainParameterType::Number:
			NumericParameters.Add(Parameter.Name, Parameter.DefaultNumber);
			break;
		case EGaeaTerrainParameterType::Integer:
			IntegerParameters.Add(Parameter.Name, Parameter.DefaultInteger);
			break;
		case EGaeaTerrainParameterType::Boolean:
			BoolParameters.Add(Parameter.Name, Parameter.DefaultBoolean);
			break;
		case EGaeaTerrainParameterType::Name:
			NameParameters.Add(Parameter.Name, Parameter.DefaultName);
			break;
		}
	}
}

void UGaeaEditorGraphNode::AllocateDefaultPins()
{
	FCreatePinParams PinParams;
	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, TerrainPinCategory, TerrainPinName, PinParams);
		if (Pin)
		{
			Pin->PinFriendlyName = FText::FromString(TEXT("In"));
		}
		return;
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (!FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor))
	{
		return;
	}

	for (const FGaeaTerrainPortDescriptor& Input : Descriptor.Inputs)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, TerrainPinCategory, Input.Name, PinParams);
		if (Pin)
		{
			Pin->PinFriendlyName = FText::FromString(TEXT("In"));
		}
	}
	for (const FGaeaTerrainPortDescriptor& Output : Descriptor.Outputs)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Output, TerrainPinCategory, Output.Name, PinParams);
		if (Pin)
		{
			Pin->PinFriendlyName = FText::FromString(TEXT("Out"));
		}
	}
}

FText UGaeaEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		return FText::FromString(TEXT("Terrain Output"));
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor))
	{
		return FText::FromString(Descriptor.DisplayName);
	}
	return FText::FromName(RecipeNodeType);
}

FText UGaeaEditorGraphNode::GetTooltipText() const
{
	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		return FText::FromString(TEXT("The terrain dataset connected here is the graph result used by Evaluate Graph and terrain outputs."));
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor))
	{
		return FText::FromString(Descriptor.Description);
	}
	return FText::GetEmpty();
}

bool UGaeaEditorGraphNode::CanUserDeleteNode() const
{
	return RecipeNodeType != GaeaEditorNodeTypes::TerrainOutput;
}

void UGaeaEditorGraphNode::PrepareForCopying()
{
	Super::PrepareForCopying();
}

void UGaeaEditorGraphNode::PostPasteNode()
{
	Super::PostPasteNode();
	RecipeNodeId = FGuid::NewGuid();
}

FGaeaGraphSchemaAction_NewNode::FGaeaGraphSchemaAction_NewNode(
	const FText& InNodeCategory,
	const FText& InMenuDesc,
	const FText& InToolTip,
	FName InNodeType)
	: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, 0)
	, NodeType(InNodeType)
{
}

UEdGraphNode* FGaeaGraphSchemaAction_NewNode::PerformAction(
	UEdGraph* ParentGraph,
	UEdGraphPin* FromPin,
	const FVector2D Location,
	bool bSelectNewNode)
{
	if (!ParentGraph || NodeType.IsNone())
	{
		return nullptr;
	}

	UGaeaEditorGraphNode* NewNode = NewObject<UGaeaEditorGraphNode>(ParentGraph);
	NewNode->Initialize(FGuid::NewGuid(), NodeType);
	NewNode->NodePosX = FMath::RoundToInt(Location.X);
	NewNode->NodePosY = FMath::RoundToInt(Location.Y);
	ParentGraph->AddNode(NewNode, true, bSelectNewNode);
	NewNode->CreateNewGuid();
	NewNode->AllocateDefaultPins();

	if (FromPin)
	{
		const UEdGraphSchema* Schema = ParentGraph->GetSchema();
		for (UEdGraphPin* Candidate : NewNode->Pins)
		{
			if (!Candidate || Candidate->Direction == FromPin->Direction)
			{
				continue;
			}
			if (Schema && Schema->TryCreateConnection(FromPin, Candidate))
			{
				break;
			}
		}
	}

	return NewNode;
}

const FPinConnectionResponse UGaeaEditorGraphSchema::CanCreateConnection(
	const UEdGraphPin* A,
	const UEdGraphPin* B) const
{
	if (!A || !B)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid terrain graph pin."));
	}

	if (A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("A node cannot connect to itself."));
	}

	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Connect an output pin to an input pin."));
	}

	if (A->PinType.PinCategory != TerrainPinCategory || B->PinType.PinCategory != TerrainPinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Terrain pins can only connect to terrain pins."));
	}

	const UEdGraphPin* OutputPin = A->Direction == EGPD_Output ? A : B;
	const UEdGraphPin* InputPin = A->Direction == EGPD_Input ? A : B;
	const UEdGraphNode* SourceNode = OutputPin->GetOwningNode();
	const UEdGraphNode* DestinationNode = InputPin->GetOwningNode();
	if (CanReachNode(DestinationNode, SourceNode))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("This connection would create a cycle."));
	}

	if (!InputPin->LinkedTo.IsEmpty())
	{
		return A == InputPin
			? FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, TEXT("Replace the existing terrain input."))
			: FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, TEXT("Replace the existing terrain input."));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
}

void UGaeaEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	TArray<FGaeaTerrainNodeDescriptor> Descriptors;
	FGaeaTerrainNodeDescriptorRegistry::GetAll(Descriptors);

	for (const FGaeaTerrainNodeDescriptor& Descriptor : Descriptors)
	{
		ContextMenuBuilder.AddAction(MakeShared<FGaeaGraphSchemaAction_NewNode>(
			FText::FromString(Descriptor.Category),
			FText::FromString(Descriptor.DisplayName),
			FText::FromString(Descriptor.Description),
			Descriptor.Type));
	}
}
