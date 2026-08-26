#if WITH_DEV_AUTOMATION_TESTS

#include "EonformEditorGraph.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace EonformEditorGraphConnectionTests
{
	UEonformEditorGraphNode* AddConnectionTestNode(UEonformEditorGraph& Graph, FName Type)
	{
		UEonformEditorGraphNode* Node = NewObject<UEonformEditorGraphNode>(&Graph);
		Node->Initialize(FGuid::NewGuid(), Type);
		Graph.AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	UEdGraphPin* FindConnectionTestPin(UEonformEditorGraphNode* Node, FName Name, EEdGraphPinDirection Direction)
	{
		return Node ? Node->FindPin(Name, Direction) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformEditorGraphConnectionContractTest,
	"Eonform.Editor.Graph.ConnectionContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformEditorGraphConnectionContractTest::RunTest(const FString& Parameters)
{
	using namespace EonformEditorGraphConnectionTests;

	UEonformEditorGraph* Graph = NewObject<UEonformEditorGraph>();
	Graph->Schema = UEonformEditorGraphSchema::StaticClass();
	const UEonformEditorGraphSchema* Schema = Cast<UEonformEditorGraphSchema>(Graph->GetSchema());
	TestNotNull(TEXT("Graph schema exists"), Schema);
	if (!Schema) return false;

	UEonformEditorGraphNode* Perlin = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::PerlinNoise);
	UEonformEditorGraphNode* Cellular = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Cellular);
	UEonformEditorGraphNode* Cellular3D = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Cellular3D);
	UEonformEditorGraphNode* Cone = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Cone);
	UEonformEditorGraphNode* Cracks = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Cracks);
	UEonformEditorGraphNode* DotNoise = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::DotNoise);
	UEonformEditorGraphNode* Draw = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Draw);
	UEonformEditorGraphNode* File = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::File);
	UEonformEditorGraphNode* Object = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Object);
	UEonformEditorGraphNode* TileInput = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::TileInput);
	UEonformEditorGraphNode* Voronoi = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Voronoi);
	UEonformEditorGraphNode* Erosion = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::HydraulicErosion);
	UEonformEditorGraphNode* Slope = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Slope);
	UEonformEditorGraphNode* Blur = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::Blur);
	UEonformEditorGraphNode* Thermal = AddConnectionTestNode(*Graph, EonformTerrainNodeTypes::ThermalErosion);
	UEonformEditorGraphNode* TerrainOutput = AddConnectionTestNode(*Graph, EonformEditorNodeTypes::TerrainOutput);

	UEdGraphPin* PerlinOut = FindConnectionTestPin(Perlin, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* CellularOut = FindConnectionTestPin(Cellular, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* Cellular3DOut = FindConnectionTestPin(Cellular3D, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* ConeOut = FindConnectionTestPin(Cone, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* CracksOut = FindConnectionTestPin(Cracks, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* DotNoiseOut = FindConnectionTestPin(DotNoise, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* DrawOut = FindConnectionTestPin(Draw, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* FileOut = FindConnectionTestPin(File, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* ObjectOut = FindConnectionTestPin(Object, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* TileInputOut = FindConnectionTestPin(TileInput, TEXT("Terrain"), EGPD_Output);
	UEdGraphPin* VoronoiOut = FindConnectionTestPin(Voronoi, TEXT("Terrain"), EGPD_Output);
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
	TestNotNull(TEXT("Cracks exposes terrain output"), CracksOut);
	TestNotNull(TEXT("DotNoise exposes terrain output"), DotNoiseOut);
	TestNotNull(TEXT("Draw exposes terrain output"), DrawOut);
	TestNotNull(TEXT("File exposes dynamic output"), FileOut);
	TestNotNull(TEXT("Object exposes terrain output"), ObjectOut);
	TestNotNull(TEXT("TileInput exposes terrain output"), TileInputOut);
	TestNotNull(TEXT("Voronoi exposes terrain output"), VoronoiOut);
	TestNotNull(TEXT("Erosion terrain input exists"), ErosionIn);
	TestNotNull(TEXT("Erosion Area heightfield input exists"), ErosionArea);
	TestNotNull(TEXT("Slope mask output exists"), SlopeMask);
	TestNotNull(TEXT("Blur Any input exists"), BlurIn);
	TestNotNull(TEXT("Blur public Out uses stable internal terrain pin"), BlurOut);
	TestNotNull(TEXT("Thermal terrain input exists"), ThermalIn);
	TestNotNull(TEXT("Terrain Output input exists"), OutputIn);
	if (!PerlinOut || !CellularOut || !Cellular3DOut || !ConeOut || !CracksOut || !DotNoiseOut || !DrawOut || !FileOut || !ObjectOut || !TileInputOut || !VoronoiOut || !ErosionIn || !ErosionArea || !SlopeMask || !BlurIn || !BlurOut || !ThermalIn || !OutputIn) return false;

	TestEqual(TEXT("Primitive visible output remains Out"), VoronoiOut->PinFriendlyName.ToString(), FString(TEXT("Out")));
	TestEqual(TEXT("Primitive output category is terrain"), VoronoiOut->PinType.PinCategory, EonformEditorGraphPins::Terrain);
	TestEqual(TEXT("File physical output is dynamic Any"), FileOut->PinType.PinCategory, EonformEditorGraphPins::Any);
	TestEqual(TEXT("Slope output category is scalar field"), SlopeMask->PinType.PinCategory, EonformEditorGraphPins::ScalarField);
	TestEqual(TEXT("Blur input is wildcard Any"), BlurIn->PinType.PinCategory, EonformEditorGraphPins::Any);

	TestTrue(TEXT("Perlin Out connects to Erosion Input"), Schema->TryCreateConnection(PerlinOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Cellular Out connects to Erosion Input"), Schema->TryCreateConnection(CellularOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Cellular3D Out connects to Erosion Input"), Schema->TryCreateConnection(Cellular3DOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Cracks Out connects to Erosion Input"), Schema->TryCreateConnection(CracksOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("DotNoise Out connects to Erosion Input"), Schema->TryCreateConnection(DotNoiseOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Draw Out connects to Erosion Input"), Schema->TryCreateConnection(DrawOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Object Out connects to Erosion Input"), Schema->TryCreateConnection(ObjectOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("TileInput Out connects to Erosion Input"), Schema->TryCreateConnection(TileInputOut, ErosionIn));
	ErosionIn->BreakAllPinLinks();
	TestTrue(TEXT("Voronoi Out connects to Erosion Input"), Schema->TryCreateConnection(VoronoiOut, ErosionIn));

	TestTrue(TEXT("Slope Mask connects to Erosion Area"), Schema->TryCreateConnection(SlopeMask, ErosionArea));
	ErosionArea->BreakAllPinLinks();
	TestTrue(TEXT("Cone heightfield connects directly to Erosion Area"), Schema->TryCreateConnection(ConeOut, ErosionArea));
	ErosionArea->BreakAllPinLinks();
	TestTrue(TEXT("Draw heightfield connects directly to Erosion Area"), Schema->TryCreateConnection(DrawOut, ErosionArea));
	ErosionArea->BreakAllPinLinks();
	TestTrue(TEXT("Object heightfield connects directly to Erosion Area"), Schema->TryCreateConnection(ObjectOut, ErosionArea));
	ErosionArea->BreakAllPinLinks();
	TestTrue(TEXT("TileInput heightfield connects directly to Erosion Area"), Schema->TryCreateConnection(TileInputOut, ErosionArea));

	const FPinConnectionResponse MaskToTerrain = Schema->CanCreateConnection(SlopeMask, ThermalIn);
	TestTrue(TEXT("Derived scalar mask does not yet promote to a terrain dataset"), MaskToTerrain.Response == CONNECT_RESPONSE_DISALLOW);

	TestTrue(TEXT("File defaults to heightfield and connects to Terrain Output"), Schema->TryCreateConnection(FileOut, OutputIn));
	OutputIn->BreakAllPinLinks();
	File->BoolParameters.Add(TEXT("IsRGB"), true);
	const FPinConnectionResponse RGBFileToTerrain = Schema->CanCreateConnection(FileOut, OutputIn);
	TestTrue(TEXT("RGB File output does not connect to Terrain Output"), RGBFileToTerrain.Response == CONNECT_RESPONSE_DISALLOW);

	TestTrue(TEXT("Terrain can connect to Any input"), Schema->TryCreateConnection(PerlinOut, BlurIn));
	BlurIn->BreakAllPinLinks();
	TestTrue(TEXT("Mask can connect to Any input"), Schema->TryCreateConnection(SlopeMask, BlurIn));

	TestTrue(TEXT("Voronoi connects to Terrain Output"), Schema->TryCreateConnection(VoronoiOut, OutputIn));
	OutputIn->BreakAllPinLinks();
	TestTrue(TEXT("Terrain-valued Any node Out connects to Terrain Output"), Schema->TryCreateConnection(BlurOut, OutputIn));

	return true;
}

#endif
