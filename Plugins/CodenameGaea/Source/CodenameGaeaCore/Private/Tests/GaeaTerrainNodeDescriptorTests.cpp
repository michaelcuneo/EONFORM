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
		TestEqual(TEXT("Source output type"), Source.Outputs[0].DataType, FName(TEXT("Terrain")));
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
	TestEqual(TEXT("Erosion has terrain and mask inputs"), Erosion.Inputs.Num(), 2);
	TestEqual(TEXT("Erosion has four outputs"), Erosion.Outputs.Num(), 4);
	TestEqual(TEXT("Erosion parameter count"), Erosion.Parameters.Num(), 9);

	if (Erosion.Inputs.Num() == 2)
	{
		TestEqual(TEXT("Erosion primary input name"), Erosion.Inputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion primary input type"), Erosion.Inputs[0].DataType, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion mask input name"), Erosion.Inputs[1].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Erosion mask input type"), Erosion.Inputs[1].DataType, FName(TEXT("ScalarField")));
	}
	if (Erosion.Outputs.Num() == 4)
	{
		TestEqual(TEXT("Erosion terrain output name"), Erosion.Outputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion terrain output type"), Erosion.Outputs[0].DataType, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion wear output"), Erosion.Outputs[1].Name, FName(TEXT("Wear")));
		TestEqual(TEXT("Erosion deposits output"), Erosion.Outputs[2].Name, FName(TEXT("Deposits")));
		TestEqual(TEXT("Erosion flow output"), Erosion.Outputs[3].Name, FName(TEXT("Flow")));
		TestEqual(TEXT("Erosion flow output type"), Erosion.Outputs[3].DataType, FName(TEXT("ScalarField")));
	}

	const FGaeaTerrainParameterDescriptor* Duration = Erosion.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("Iterations"));
		});
	TestNotNull(TEXT("Duration parameter exists"), Duration);
	if (Duration)
	{
		TestEqual(TEXT("Duration display name"), Duration->DisplayName, FString(TEXT("Duration")));
		TestEqual(TEXT("Duration type"), Duration->Type, EGaeaTerrainParameterType::Integer);
		TestEqual(TEXT("Duration default"), Duration->DefaultInteger, static_cast<int64>(24));
		TestTrue(TEXT("Duration has minimum"), Duration->bHasMinimum);
		TestTrue(TEXT("Duration has maximum"), Duration->bHasMaximum);
		TestEqual(TEXT("Duration minimum"), Duration->Minimum, 1.0);
		TestEqual(TEXT("Duration maximum"), Duration->Maximum, 4096.0);
	}

	const FGaeaTerrainParameterDescriptor* Strength = Erosion.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("Strength"));
		});
	const FGaeaTerrainParameterDescriptor* RockSoftness = Erosion.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("RockSoftness"));
		});
	TestNotNull(TEXT("Strength parameter exists"), Strength);
	TestNotNull(TEXT("Rock Softness parameter exists"), RockSoftness);

	FGaeaTerrainNodeDescriptor Context;
	TestTrue(TEXT("TerrainContext descriptor still exists for legacy recipes"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::TerrainContext, Context));
	TestTrue(TEXT("TerrainContext is hidden from graph authoring"), Context.bHiddenInGraph);

	FGaeaTerrainNodeDescriptor Geology;
	TestTrue(TEXT("Geology descriptor still exists for legacy recipes"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Geology, Geology));
	TestTrue(TEXT("Geology is hidden from graph authoring"), Geology.bHiddenInGraph);

	FGaeaTerrainNodeDescriptor Masks;
	TestTrue(TEXT("ProcessMasks descriptor still exists for legacy recipes"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ProcessMasks, Masks));
	TestTrue(TEXT("ProcessMasks is hidden from graph authoring"), Masks.bHiddenInGraph);

	TArray<FGaeaTerrainNodeDescriptor> AllDescriptors;
	FGaeaTerrainNodeDescriptorRegistry::GetAll(AllDescriptors);
	TestTrue(TEXT("Built-in descriptor list includes at least three nodes"), AllDescriptors.Num() >= 3);
	return true;
}

#endif
