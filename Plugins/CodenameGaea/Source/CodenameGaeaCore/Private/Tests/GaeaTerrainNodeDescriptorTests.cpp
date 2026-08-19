#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainNodeDescriptorRegistryTest,
	"CodenameGaea.Core.Graph.NodeDescriptors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainNodeDescriptorRegistryTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Source;
	TestTrue(
		TEXT("SourceDataset descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::SourceDataset, Source));
	TestEqual(TEXT("Source has no inputs"), Source.Inputs.Num(), 0);
	TestEqual(TEXT("Source has one output"), Source.Outputs.Num(), 1);
	if (Source.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Source output name"), Source.Outputs[0].Name, FName(TEXT("Terrain")));
	}

	FGaeaTerrainNodeDescriptor Procedural;
	TestTrue(
		TEXT("ProceduralTerrain descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ProceduralTerrain, Procedural));
	TestEqual(TEXT("Procedural terrain has no inputs"), Procedural.Inputs.Num(), 0);
	TestEqual(TEXT("Procedural terrain has one output"), Procedural.Outputs.Num(), 1);
	TestEqual(TEXT("Procedural terrain parameter count"), Procedural.Parameters.Num(), 8);

	FGaeaTerrainNodeDescriptor Erosion;
	TestTrue(
		TEXT("HydraulicErosion descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::HydraulicErosion, Erosion));
	TestEqual(TEXT("Erosion has one input"), Erosion.Inputs.Num(), 1);
	TestEqual(TEXT("Erosion has one output"), Erosion.Outputs.Num(), 1);
	TestEqual(TEXT("Erosion parameter count"), Erosion.Parameters.Num(), 8);

	const FGaeaTerrainParameterDescriptor* Iterations = Erosion.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("Iterations"));
		});
	TestNotNull(TEXT("Iterations parameter exists"), Iterations);
	if (Iterations)
	{
		TestEqual(TEXT("Iterations type"), Iterations->Type, EGaeaTerrainParameterType::Integer);
		TestEqual(TEXT("Iterations default"), Iterations->DefaultInteger, static_cast<int64>(24));
		TestTrue(TEXT("Iterations has minimum"), Iterations->bHasMinimum);
		TestTrue(TEXT("Iterations has maximum"), Iterations->bHasMaximum);
		TestEqual(TEXT("Iterations minimum"), Iterations->Minimum, 1.0);
		TestEqual(TEXT("Iterations maximum"), Iterations->Maximum, 4096.0);
	}

	TArray<FGaeaTerrainNodeDescriptor> AllDescriptors;
	FGaeaTerrainNodeDescriptorRegistry::GetAll(AllDescriptors);
	TestTrue(TEXT("Built-in descriptor list includes at least three nodes"), AllDescriptors.Num() >= 3);
	return true;
}

#endif
