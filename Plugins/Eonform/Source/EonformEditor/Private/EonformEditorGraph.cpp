#include "EonformEditorGraph.h"

#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformEditorGraphPins
{
	const FName Terrain(TEXT("EonformTerrain"));
	const FName ScalarField(TEXT("EonformScalarField"));
	const FName Color(TEXT("EonformColor"));
	const FName Any(TEXT("EonformAny"));
}

namespace EonformEditorNodeTypes
{
	const FName TerrainOutput(TEXT("__TerrainOutput"));
}

namespace
{
	const FName TerrainPinName(TEXT("Terrain"));

	FName PinCategoryForDataType(FName DataType)
	{
		if (DataType == TEXT("ScalarField")) return EonformEditorGraphPins::ScalarField;
		if (DataType == TEXT("Color")) return EonformEditorGraphPins::Color;
		if (DataType == TEXT("Any")) return EonformEditorGraphPins::Any;
		return EonformEditorGraphPins::Terrain;
	}

	bool IsSupportedPinCategory(FName Category)
	{
		return Category == EonformEditorGraphPins::Terrain
			|| Category == EonformEditorGraphPins::ScalarField
			|| Category == EonformEditorGraphPins::Color
			|| Category == EonformEditorGraphPins::Any;
	}

	FName EffectivePinCategory(const UEdGraphPin* Pin)
	{
		if (!Pin) return NAME_None;
		FName Category = Pin->PinType.PinCategory;
		if (Category != EonformEditorGraphPins::Any || Pin->Direction != EGPD_Output) return Category;

		const UEonformEditorGraphNode* Node = Cast<UEonformEditorGraphNode>(Pin->GetOwningNode());
		if (Node && Node->RecipeNodeType == EonformTerrainNodeTypes::Constant)
		{
			const FName Mode = Node->NameParameters.FindRef(TEXT("Output"));
			return Mode == TEXT("Color") ? EonformEditorGraphPins::Color : EonformEditorGraphPins::Terrain;
		}
		if (Node && Node->RecipeNodeType == EonformTerrainNodeTypes::File)
		{
			return Node->BoolParameters.FindRef(TEXT("IsRGB"))
				? EonformEditorGraphPins::Color
				: EonformEditorGraphPins::Terrain;
		}
		return Category;
	}

	bool ArePinCategoriesCompatible(FName OutputCategory, FName InputCategory)
	{
		if (OutputCategory == InputCategory) return true;
		if (OutputCategory == EonformEditorGraphPins::Any || InputCategory == EonformEditorGraphPins::Any) return true;
		return OutputCategory == EonformEditorGraphPins::Terrain
			&& InputCategory == EonformEditorGraphPins::ScalarField;
	}

	FText FriendlyPinName(const FEonformTerrainPortDescriptor& Port, EEdGraphPinDirection Direction)
	{
		if (!Port.DisplayName.IsEmpty()) return FText::FromString(Port.DisplayName);
		if (Port.DataType == TEXT("Terrain")) return FText::FromString(Direction == EGPD_Input ? TEXT("In") : TEXT("Out"));
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

void UEonformEditorGraphNode::Initialize(const FGuid& InRecipeNodeId, FName InRecipeNodeType)
{
	RecipeNodeId = InRecipeNodeId;
	RecipeNodeType = InRecipeNodeType;
	InitializeParameterDefaults();
}

void UEonformEditorGraphNode::InitializeParameterDefaults()
{
	NumericParameters.Reset();
	IntegerParameters.Reset();
	BoolParameters.Reset();
	NameParameters.Reset();
	ColorParameters.Reset();
	if (RecipeNodeType == EonformEditorNodeTypes::TerrainOutput) return;

	FEonformTerrainNodeDescriptor Descriptor;
	if (!FEonformTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return;
	for (const FEonformTerrainParameterDescriptor& Parameter : Descriptor.Parameters)
	{
		switch (Parameter.Type)
		{
		case EEonformTerrainParameterType::Number: NumericParameters.Add(Parameter.Name, Parameter.DefaultNumber); break;
		case EEonformTerrainParameterType::Integer: IntegerParameters.Add(Parameter.Name, Parameter.DefaultInteger); break;
		case EEonformTerrainParameterType::Boolean: BoolParameters.Add(Parameter.Name, Parameter.DefaultBoolean); break;
		case EEonformTerrainParameterType::Name: NameParameters.Add(Parameter.Name, Parameter.DefaultName); break;
		case EEonformTerrainParameterType::Color: ColorParameters.Add(Parameter.Name, Parameter.DefaultColor); break;
		case EEonformTerrainParameterType::Range:
			NumericParameters.Add(FName(*(Parameter.Name.ToString() + TEXT("Min"))), Parameter.DefaultRangeMin);
			NumericParameters.Add(FName(*(Parameter.Name.ToString() + TEXT("Max"))), Parameter.DefaultRangeMax);
			break;
		}
	}
}

void UEonformEditorGraphNode::AllocateDefaultPins()
{
	FCreatePinParams PinParams;
	if (RecipeNodeType == EonformEditorNodeTypes::TerrainOutput)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, EonformEditorGraphPins::Terrain, TerrainPinName, PinParams);
		if (Pin) Pin->PinFriendlyName = FText::FromString(TEXT("In"));
		return;
	}

	FEonformTerrainNodeDescriptor Descriptor;
	if (!FEonformTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return;
	for (const FEonformTerrainPortDescriptor& Input : Descriptor.Inputs)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, PinCategoryForDataType(Input.DataType), Input.Name, PinParams);
		if (Pin) Pin->PinFriendlyName = FriendlyPinName(Input, EGPD_Input);
	}
	for (const FEonformTerrainPortDescriptor& Output : Descriptor.Outputs)
	{
		// Terrain and dynamic Any outputs preserve the editor's historical
		// physical "Terrain" pin identity. Any may resolve to terrain at runtime
		// (File, Constant, Blur, utility routing nodes), so changing that identity
		// would break existing authored graphs and connection contracts.
		//
		// Explicit ScalarField/Color outputs keep their real "Out" identity. This
		// is required for nodes such as DataExtractor, which intentionally exposes
		// both scalar Out and an exact Terrain passthrough on the same node.
		const bool bTerrainOutAlias = Output.Name == TEXT("Out")
			&& (Output.DataType == TEXT("Terrain") || Output.DataType == TEXT("Any"));
		const FName EditorPinName = bTerrainOutAlias ? TerrainPinName : Output.Name;
		UEdGraphPin* Pin = CreatePin(EGPD_Output, PinCategoryForDataType(Output.DataType), EditorPinName, PinParams);
		if (Pin) Pin->PinFriendlyName = FriendlyPinName(Output, EGPD_Output);
	}
}

FText UEonformEditorGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (RecipeNodeType == EonformEditorNodeTypes::TerrainOutput) return FText::FromString(TEXT("Terrain Output"));
	FEonformTerrainNodeDescriptor Descriptor;
	if (FEonformTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return FText::FromString(Descriptor.DisplayName);
	return FText::FromName(RecipeNodeType);
}

FText UEonformEditorGraphNode::GetTooltipText() const
{
	if (RecipeNodeType == EonformEditorNodeTypes::TerrainOutput)
	{
		return FText::FromString(TEXT("The terrain connected here is the graph result used by Evaluate Graph and terrain outputs."));
	}
	FEonformTerrainNodeDescriptor Descriptor;
	if (FEonformTerrainNodeDescriptorRegistry::Get(RecipeNodeType, Descriptor)) return FText::FromString(Descriptor.Description);
	return FText::GetEmpty();
}

bool UEonformEditorGraphNode::CanUserDeleteNode() const
{
	return RecipeNodeType != EonformEditorNodeTypes::TerrainOutput;
}

void UEonformEditorGraphNode::PrepareForCopying()
{
	Super::PrepareForCopying();
}

void UEonformEditorGraphNode::PostPasteNode()
{
	Super::PostPasteNode();
	RecipeNodeId = FGuid::NewGuid();
}

FEonformGraphSchemaAction_NewNode::FEonformGraphSchemaAction_NewNode(
	const FText& InNodeCategory,
	const FText& InMenuDesc,
	const FText& InToolTip,
	FName InNodeType)
	: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, 0)
	, NodeType(InNodeType)
{
}

UEdGraphNode* FEonformGraphSchemaAction_NewNode::PerformAction(
	UEdGraph* ParentGraph,
	UEdGraphPin* FromPin,
	const FVector2D Location,
	bool bSelectNewNode)
{
	if (!ParentGraph || NodeType.IsNone()) return nullptr;
	UEonformEditorGraphNode* NewNode = NewObject<UEonformEditorGraphNode>(ParentGraph);
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

const FPinConnectionResponse UEonformEditorGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (!A || !B) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid graph pin."));
	if (A->GetOwningNode() == B->GetOwningNode()) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("A node cannot connect to itself."));
	if (A->Direction == B->Direction) return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Connect an output pin to an input pin."));
	if (!IsSupportedPinCategory(A->PinType.PinCategory) || !IsSupportedPinCategory(B->PinType.PinCategory))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Unsupported EONFORM graph value type."));
	}

	const UEdGraphPin* OutputPin = A->Direction == EGPD_Output ? A : B;
	const UEdGraphPin* InputPin = A->Direction == EGPD_Input ? A : B;
	if (!ArePinCategoriesCompatible(EffectivePinCategory(OutputPin), EffectivePinCategory(InputPin)))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("The selected output type is incompatible with this input."));
	}
	if (CanReachNode(InputPin->GetOwningNode(), OutputPin->GetOwningNode()))
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

void UEonformEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	TArray<FEonformTerrainNodeDescriptor> Descriptors;
	FEonformTerrainNodeDescriptorRegistry::GetAll(Descriptors);
	for (const FEonformTerrainNodeDescriptor& Descriptor : Descriptors)
	{
		if (Descriptor.bHiddenInGraph) continue;
		ContextMenuBuilder.AddAction(MakeShared<FEonformGraphSchemaAction_NewNode>(
			FText::FromString(Descriptor.Category),
			FText::FromString(Descriptor.DisplayName),
			FText::FromString(Descriptor.Description),
			Descriptor.Type));
	}
}