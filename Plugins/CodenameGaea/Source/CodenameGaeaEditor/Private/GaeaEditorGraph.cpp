#include "GaeaEditorGraph.h"

#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaEditorGraphPins
{
	const FName Terrain(TEXT("GaeaTerrain"));
	const FName ScalarField(TEXT("GaeaScalarField"));
	const FName Any(TEXT("GaeaAny"));
}

namespace GaeaEditorNodeTypes
{
	const FName TerrainOutput(TEXT("__TerrainOutput"));
}

namespace
{
	const FName TerrainPinName(TEXT("Terrain"));

	FName PinCategoryForDataType(FName DataType)
	{
		if (DataType == TEXT("ScalarField")) return GaeaEditorGraphPins::ScalarField;
		if (DataType == TEXT("Any")) return GaeaEditorGraphPins::Any;
		return GaeaEditorGraphPins::Terrain;
	}

	bool IsSupportedPinCategory(FName Category)
	{
		return Category == GaeaEditorGraphPins::Terrain
			|| Category == GaeaEditorGraphPins::ScalarField
			|| Category == GaeaEditorGraphPins::Any;
	}

	bool ArePinCategoriesCompatible(FName A, FName B)
	{
		if (A == B) return true;
		return A == GaeaEditorGraphPins::Any || B == GaeaEditorGraphPins::Any;
	}

	FText FriendlyPinName(const FGaeaTerrainPortDescriptor& Port, EEdGraphPinDirection Direction)
	{
		if (!Port.DisplayName.IsEmpty())
		{
			return FText::FromString(Port.DisplayName);
		}
		if (Port.DataType == TEXT("Terrain"))
		{
			return FText::FromString(Direction == EGPD_Input ? TEXT("In") : TEXT("Out"));
		}
		return FText::FromName(Port.Name);
	}

	bool CanReachNode(const UEdGraphNode* StartNode, const UEdGraphNode* TargetNode)
	{
		if (!StartNode || !TargetNode) return false;

		TArray<const UEdGraphNode*> Stack;
		TSet<const UEdGraphNode*> Visited;
		Stack.Add(StartNode);

		while (!Stack.IsEmpty())
		{
			const UEdGraphNode* Current = Stack.Pop(EAllowShrinking::No);
			if (!Current || Visited.Contains(Current)) continue;
			if (Current == TargetNode) return true;

			Visited.Add(Current);
			for (const UEdGraphPin* Pin : Current->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin && LinkedPin->GetOwningNode()) Stack.Add(LinkedPin->GetOwningNode());
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

	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput) return;

	FGaeaTerrainNodeDescriptor Descriptor;
	if (!FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return;

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
		case EGaeaTerrainParameterType::Range:
			NumericParameters.Add(FName(*(Parameter.Name.ToString() + TEXT("Min"))), Parameter.DefaultRangeMin);
			NumericParameters.Add(FName(*(Parameter.Name.ToString() + TEXT("Max"))), Parameter.DefaultRangeMax);
			break;
		}
	}
}

void UGaeaEditorGraphNode::AllocateDefaultPins()
{
	FCreatePinParams PinParams;
	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, GaeaEditorGraphPins::Terrain, TerrainPinName, PinParams);
		if (Pin) Pin->PinFriendlyName = FText::FromString(TEXT("In"));
		return;
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (!FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return;

	for (const FGaeaTerrainPortDescriptor& Input : Descriptor.Inputs)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, PinCategoryForDataType(Input.DataType), Input.Name, PinParams);
		if (Pin) Pin->PinFriendlyName = FriendlyPinName(Input, EGPD_Input);
	}
	for (const FGaeaTerrainPortDescriptor& Output : Descriptor.Outputs)
	{
		// The public Gaea-facing output is named Out, while the editor recipe has
		// historically used Terrain as the terminal terrain channel. Keep that
		// stable internal name so save/reopen and Terrain Output reconstruction
		// work end-to-end, while the visible pin remains exactly "Out".
		const FName EditorPinName = Output.Name == TEXT("Out") ? TerrainPinName : Output.Name;
		UEdGraphPin* Pin = CreatePin(EGPD_Output, PinCategoryForDataType(Output.DataType), EditorPinName, PinParams);
		if (Pin) Pin->PinFriendlyName = FriendlyPinName(Output, EGPD_Output);
	}
}

FText UGaeaEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput) return FText::FromString(TEXT("Terrain Output"));

	FGaeaTerrainNodeDescriptor Descriptor;
	if (FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return FText::FromString(Descriptor.DisplayName);
	return FText::FromName(RecipeNodeType);
}

FText UGaeaEditorGraphNode::GetTooltipText() const
{
	if (RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		return FText::FromString(TEXT("The terrain connected here is the graph result used by Evaluate Graph and terrain outputs."));
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	if (FGaeaTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return FText::FromString(Descriptor.Description);
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
	if (!ParentGraph || NodeType.IsNone()) return nullptr;

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
			if (!Candidate || Candidate->Direction == FromPin->Direction) continue;
			if (Schema && Schema->TryCreateConnection(FromPin, Candidate)) break;
		}
	}
	return NewNode;
}

const FPinConnectionResponse UGaeaEditorGraphSchema::CanCreateConnection(
	const UEdGraphPin* A,
	const UEdGraphPin* B) const
{
	if (!A || !B) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid graph pin."));
	if (A->GetOwningNode() == B->GetOwningNode()) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("A node cannot connect to itself."));
	if (A->Direction == B->Direction) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Connect an output pin to an input pin."));

	if (!IsSupportedPinCategory(A->PinType.PinCategory) || !IsSupportedPinCategory(B->PinType.PinCategory))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Unsupported EONFORM graph value type."));
	}
	if (!ArePinCategoriesCompatible(A->PinType.PinCategory, B->PinType.PinCategory))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Terrain and mask data are different value types. Use an Any-compatible node or the appropriate mask input."));
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
			? FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, TEXT("Replace the existing input."))
			: FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, TEXT("Replace the existing input."));
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
}

void UGaeaEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	TArray<FGaeaTerrainNodeDescriptor> Descriptors;
	FGaeaTerrainNodeDescriptorRegistry::GetAll(Descriptors);

	for (const FGaeaTerrainNodeDescriptor& Descriptor : Descriptors)
	{
		if (Descriptor.bHiddenInGraph) continue;
		ContextMenuBuilder.AddAction(MakeShared<FGaeaGraphSchemaAction_NewNode>(
			FText::FromString(Descriptor.Category),
			FText::FromString(Descriptor.DisplayName),
			FText::FromString(Descriptor.Description),
			Descriptor.Type));
	}
}
