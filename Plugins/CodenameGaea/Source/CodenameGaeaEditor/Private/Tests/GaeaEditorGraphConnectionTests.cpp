#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaEditorGraph.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace GaeaEditorGraphConnectionTests
{
	UGaeaEditorGraphNode* AddConnectionTestNode(UGaeaEditorGraph& Graph, FName Type)
	{
		UGaeaEditorGraphNode* Node = NewObject<UGaeaEditorGraphNode>(&Graph);
		Node->Initialize(FGuid::NewGuid(), Type);
		Graph.AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	UEdGraphPin* FindConnectionTestPin(UGaeaEditorGraphNode* Node, FName Name, EEdGraphPinDirection Direction)
	{
		return Node ? Node->FindPin(Name, Direction) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaEditorGraphConnectionContractTest,
	"CodenameGaea.Editor.Graph.ConnectionContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaEditorGraphConnectionContractTest::RunTest(const FString& Parameters)
{
	using namespace GaeaEditorGraphConnectionTests;

	UGaeaEditorGraph* Graph = NewObject<UGaeaEditorGraph>();
	Graph->Schema = UGaeaEditorGraphSchema::StaticClass();
	const UGaeaEditorGraphSchema* Schema = Cast<UGaeaEditorGraphSchema>(Graph->GetSchema());
	TestNotNull(TEXT("Graph schema exists"), Schema);
	if (!Schema) return false;

	UGaeaEditorGraphNode* Perlin = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::PerlinNoise);
	UGaeaEditorGraphNode* Cellular = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::Cellular);
	UGaeaEditorGraphNode* Cellular3D = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::Cellular3D);
	UGaeaEditorGraphNode* Cone = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::Cone);
	UGaeaEditorGraphNode* Erosion = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::HydraulicErosion);
	UGaeaEditorGraphNode* Slope = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::Slope);
	UGaeaEditorGraphNode* Blur = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::Blur);
	UGaeaEditorGraphNode* Thermal = AddConnectionTestNode(*Graph, GaeaTerrainNodeTypes::ThermalErosion);
	UGaeaEditorGraphNode* TerrainOutput = AddConnectionTestNode(*Graph, GaeaEditorNodeTypes::TerrainOutput);

	UEdGraphPin* PerlinOut = FindConnectionTestPin(Perlin, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* CellularOut = FindConnectionTestPin(Cellular, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* Cellular3DOut = FindConnectionTestPin(Cellular3D, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* ConeOut = FindConnectionTestPin(Cone, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* ErosionIn = FindConnectionTestPin(Erosion, TEXT("Terrain"), EGPD_Input);
	UEdGraphPin* ErosionArea = FindConnectionTestPin(Erosion, TEXT("Area"), EGPD_Input);
	UEdGraphPin* SlopeMask = FindConnectionTestPin(Slope, TEXT("Mask"), EGPD_Output);
	UEdGraphPin* BlurIn = FindConnectionTestPin(Blur, TEXT("Input"), EGPD_Input);
	UEdGraphPin* BlurOut = FindConnectionTestPin(Blur, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* ThermalIn = FindConnectionTestPin(Thermal, TEXT("Terrain"), EGPD_Input);
	UEdGraphPin* OutputIn = FindConnectionTestPin(TerrainOutput, TEXT("Terrain"), EGPD_Input);

	TestNotNull(TEXT("Perlin exposes terrain output"), PerlinOut);
	TestNotNull(TEXT("Cellular exposes terrain output"), CellularOut);
	TestNotNull(TEXT("Cellular3D exposes terrain output"), Cellular3DOut);
	TestNotNull(TEXT("Cone exposes terrain output"), ConeOut);
	TestNotNull(TEXT("Erosion terrain input exists"), ErosionIn);
	TestNotNull(TEXT("Erosion Area heightfield input exists"), ErosionArea);
	TestNotNull(TEXT("Slope mask output exists"), SlopeMask);
	TestNotNull(TEXT("Blur Any input exists"), BlurIn);
	TestNotNull(TEXT("Blur public Out uses stable internal terrain pin"), BlurOut);
	TestNotNull(TEXT("Thermal terrain input exists"), ThermalIn);
	TestNotNull(TEXT("Terrain Output input exists"), OutputIn);
	if (!PerlinOut || !CellularOut || !Cellular3DOut || !ConeOut || !ErosionIn || !ErosionArea || !SlopeMask || !BlurIn || !BlurOut || !ThermalIn || !OutputIn) return false;

	TestEqual(TEXT("Primitive visible output remains Out"), ConeOut->PinFriendlyName.ToString(), FString(TEXT("Out")));
	TestEqual(TEXT("Primitive output category is terrain"), ConeOut->PinType.PinCategory, GaeaEditorGraphPins::Terrain);
	TestEqual(TEXT("Slope output category is scalar field"), SlopeMask->PinType.PinCategory, GaeaEditorGraphPins::ScalarField);
	TestEqual(TEXT("Blur input is wildcard Any"), BlurIn->PinType.PinCategory, GaeaEditorGraphPins::Any);

	TestTrue(TEXT("Perlin Out connects to Erosion Input"), Schema->TryCreateConnection(PerlinOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Cellular Out connects to Erosion Input"), Schema->TryCreateConnection(CellularOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Cellular3D Out connects to Erosion Input"), Schema->TryCreateConnection(Cellular3DOut, ErosionIn));

	TestTrue(TEXT("Slope Mask connects to Erosion Area"), Schema->TryCreateConnection(SlopeMask, ErosionArea));
	ErosionArea->BreakAllPinLinks();
	TestTrue(TEXT("Cone heightfield connects directly to Erosion Area"), Schema->TryCreateConnection(ConeOut, ErosionArea));

	const FPinConnectionResponse MaskToTerrain = Schema->CanCreateConnection(SlopeMask, ThermalIn);
	TestTrue(TEXT("Derived scalar mask does not yet promote to a terrain dataset"), MaskToTerrain.Response == CONNECT_RESPONSE_DISALLOW);

	TestTrue(TEXT("Terrain can connect to Any input"), Schema->TryCreateConnection(PerlinOut, BlurIn));
	BlurIn->BreakAllPinLinks();
	TestTrue(TEXT("Mask can connect to Any input"), Schema->TryCreateConnection(SlopeMask, BlurIn));

	TestTrue(TEXT("Cone connects to Terrain Output"), Schema->TryCreateConnection(ConeOut, OutputIn));
	OutputIn->BreakAllPinLinks();
	TestTrue(TEXT("Terrain-valued Any node Out connects to Terrain Output"), Schema->TryCreateConnection(BlurOut, OutputIn));

	return true;
}

#endif
