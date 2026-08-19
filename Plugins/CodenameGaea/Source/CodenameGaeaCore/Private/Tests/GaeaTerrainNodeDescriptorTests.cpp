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
	TestEqual(TEXT("Erosion has three inputs"), Erosion.Inputs.Num(), 3);
	TestEqual(TEXT("Erosion has four outputs"), Erosion.Outputs.Num(), 4);
	TestEqual(TEXT("Erosion parameter count"), Erosion.Parameters.Num(), 14);

	if (Erosion.Inputs.Num() == 3)
	{
		TestEqual(TEXT("Erosion primary input name"), Erosion.Inputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion primary input display"), Erosion.Inputs[0].DisplayName, FString(TEXT("Input")));
		TestEqual(TEXT("Erosion area input name"), Erosion.Inputs[1].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Erosion area input display"), Erosion.Inputs[1].DisplayName, FString(TEXT("Area Mask")));
		TestEqual(TEXT("Erosion sediment input name"), Erosion.Inputs[2].Name, FName(TEXT("Sediment")));
		TestEqual(TEXT("Erosion sediment input type"), Erosion.Inputs[2].DataType, FName(TEXT("ScalarField")));
	}
	if (Erosion.Outputs.Num() == 4)
	{
		TestEqual(TEXT("Erosion terrain output name"), Erosion.Outputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion terrain output display"), Erosion.Outputs[0].DisplayName, FString(TEXT("Out")));
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
		TestEqual(TEXT("Duration group"), Duration->Group, FString(TEXT("Erosion")));
		TestEqual(TEXT("Duration display name"), Duration->DisplayName, FString(TEXT("Duration")));
		TestEqual(TEXT("Duration type"), Duration->Type, EGaeaTerrainParameterType::Integer);
	}

	const FGaeaTerrainParameterDescriptor* Selective = Erosion.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("SelectiveProcessing"));
		});
	TestNotNull(TEXT("Selective Processing parameter exists"), Selective);
	if (Selective)
	{
		TestEqual(TEXT("Selective Processing type"), Selective->Type, EGaeaTerrainParameterType::Name);
		TestEqual(TEXT("Selective Processing default"), Selective->DefaultName, FName(TEXT("None")));
		TestEqual(TEXT("Selective Processing option count"), Selective->NameOptions.Num(), 4);
		if (Selective->NameOptions.Num() == 4)
		{
			TestEqual(TEXT("Selective option None"), Selective->NameOptions[0], FName(TEXT("None")));
			TestEqual(TEXT("Selective option Erosion Strength"), Selective->NameOptions[1], FName(TEXT("ErosionStrength")));
			TestEqual(TEXT("Selective option Rock Softness"), Selective->NameOptions[2], FName(TEXT("RockSoftness")));
			TestEqual(TEXT("Selective option Precipitation"), Selective->NameOptions[3], FName(TEXT("Precipitation")));
		}
	}

	TestNotNull(TEXT("Downcutting parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Downcutting")); }));
	TestNotNull(TEXT("Inhibition parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Inhibition")); }));
	TestNotNull(TEXT("Base Level parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("BaseLevel")); }));
	TestNotNull(TEXT("Feature Scale parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("FeatureScale")); }));
	TestNotNull(TEXT("Debris parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Debris")); }));
	TestNotNull(TEXT("Volume parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Volume")); }));
	TestNotNull(TEXT("Sediment Removal parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("SedimentRemoval")); }));
	TestNotNull(TEXT("Seed parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Seed")); }));
	TestNotNull(TEXT("Aggressive Mode parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("AggressiveMode")); }));
	TestNotNull(TEXT("Deterministic parameter exists"), Erosion.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Deterministic")); }));

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
