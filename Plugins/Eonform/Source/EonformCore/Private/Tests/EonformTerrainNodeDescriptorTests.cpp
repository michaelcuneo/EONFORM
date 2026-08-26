#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainNodeDescriptorRegistryTest,
	"Eonform.Core.Graph.NodeDescriptors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainNodeDescriptorRegistryTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Source;
	TestTrue(TEXT("SourceDataset descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::SourceDataset, Source));
	TestEqual(TEXT("Source has no inputs"), Source.Inputs.Num(), 0);
	TestEqual(TEXT("Source has one output"), Source.Outputs.Num(), 1);
	if (Source.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Source output name"), Source.Outputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Source output type"), Source.Outputs[0].DataType, FName(TEXT("Terrain")));
	}
	TestTrue(TEXT("SourceDataset is hidden from graph authoring"), Source.bHiddenInGraph);

	FEonformTerrainNodeDescriptor Perlin;
	TestTrue(TEXT("Perlin descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::PerlinNoise, Perlin));
	TestEqual(TEXT("Perlin display name"), Perlin.DisplayName, FString(TEXT("Perlin")));
	TestEqual(TEXT("Perlin category"), Perlin.Category, FString(TEXT("Primitive")));
	TestEqual(TEXT("Perlin has no inputs"), Perlin.Inputs.Num(), 0);
	TestEqual(TEXT("Perlin has one output"), Perlin.Outputs.Num(), 1);
	TestEqual(TEXT("Perlin current parameter count"), Perlin.Parameters.Num(), 14);

	FEonformTerrainNodeDescriptor Erosion;
	TestTrue(TEXT("Erosion descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::HydraulicErosion, Erosion));
	TestEqual(TEXT("Erosion category"), Erosion.Category, FString(TEXT("Simulate")));
	TestEqual(TEXT("Erosion has current two inputs"), Erosion.Inputs.Num(), 2);
	TestEqual(TEXT("Erosion has four outputs"), Erosion.Outputs.Num(), 4);
	TestEqual(TEXT("Erosion current parameter count"), Erosion.Parameters.Num(), 20);
	if (Erosion.Inputs.Num() == 2)
	{
		TestEqual(TEXT("Erosion terrain input"), Erosion.Inputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Erosion Area input"), Erosion.Inputs[1].Name, FName(TEXT("Area")));
		TestEqual(TEXT("Erosion Area input type"), Erosion.Inputs[1].DataType, FName(TEXT("ScalarField")));
	}
	if (Erosion.Outputs.Num() == 4)
	{
		TestEqual(TEXT("Erosion terrain output"), Erosion.Outputs[0].Name, FName(TEXT("Out")));
		TestEqual(TEXT("Erosion wear output"), Erosion.Outputs[1].Name, FName(TEXT("Wear")));
		TestEqual(TEXT("Erosion deposits output"), Erosion.Outputs[2].Name, FName(TEXT("Deposits")));
		TestEqual(TEXT("Erosion flow output"), Erosion.Outputs[3].Name, FName(TEXT("Flow")));
	}
	const FEonformTerrainParameterDescriptor* Duration = Erosion.Parameters.FindByPredicate(
		[](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Duration")); });
	TestNotNull(TEXT("Erosion Duration parameter exists"), Duration);
	const FEonformTerrainParameterDescriptor* AreaEffect = Erosion.Parameters.FindByPredicate(
		[](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("AreaEffect")); });
	TestNotNull(TEXT("Erosion Area Effect parameter exists"), AreaEffect);
	if (AreaEffect)
	{
		TestTrue(TEXT("Area Effect includes Erosion Strength"), AreaEffect->NameOptions.Contains(TEXT("Erosion Strength")));
		TestTrue(TEXT("Area Effect includes Rock Softness"), AreaEffect->NameOptions.Contains(TEXT("Rock Softness")));
		TestTrue(TEXT("Area Effect includes Precipitation Amount"), AreaEffect->NameOptions.Contains(TEXT("Precipitation Amount")));
		TestTrue(TEXT("Area Effect includes None"), AreaEffect->NameOptions.Contains(TEXT("None")));
	}
	TestNotNull(TEXT("Erosion Real Scale exists"), Erosion.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("RealScale")); }));
	TestNotNull(TEXT("Erosion Terrain Scale exists"), Erosion.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("TerrainScale")); }));
	TestNotNull(TEXT("Erosion Verticality exists"), Erosion.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Verticality")); }));
	TestNotNull(TEXT("Erosion Bias Type exists"), Erosion.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("BiasType")); }));
	TestNotNull(TEXT("Erosion Bias exists"), Erosion.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Bias")); }));
	TestNotNull(TEXT("Erosion Reverse exists"), Erosion.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Reverse")); }));

	FEonformTerrainNodeDescriptor Curvature;
	TestTrue(TEXT("Curvature descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Curvature, Curvature));
	TestEqual(TEXT("Curvature category"), Curvature.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("Curvature parameter count"), Curvature.Parameters.Num(), 3);
	TestEqual(TEXT("Curvature input count"), Curvature.Inputs.Num(), 1);
	TestEqual(TEXT("Curvature output count"), Curvature.Outputs.Num(), 1);
	if (Curvature.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Curvature output name"), Curvature.Outputs[0].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Curvature output type"), Curvature.Outputs[0].DataType, FName(TEXT("ScalarField")));
	}
	TestNotNull(TEXT("Curvature Range exists"), Curvature.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Range")); }));
	TestNotNull(TEXT("Curvature Type exists"), Curvature.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Type")); }));
	TestNotNull(TEXT("Curvature Falloff exists"), Curvature.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Falloff")); }));

	FEonformTerrainNodeDescriptor Height;
	TestTrue(TEXT("Height descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Height, Height));
	TestEqual(TEXT("Height category"), Height.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("Height parameter count"), Height.Parameters.Num(), 2);
	TestEqual(TEXT("Height input count"), Height.Inputs.Num(), 1);
	TestEqual(TEXT("Height output count"), Height.Outputs.Num(), 1);
	if (Height.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Height output name"), Height.Outputs[0].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Height output type"), Height.Outputs[0].DataType, FName(TEXT("ScalarField")));
	}
	TestNotNull(TEXT("Height Range exists"), Height.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Range")); }));
	TestNotNull(TEXT("Height Falloff exists"), Height.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Falloff")); }));

	FEonformTerrainNodeDescriptor Context;
	TestTrue(TEXT("TerrainContext descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::TerrainContext, Context));
	TestTrue(TEXT("TerrainContext is hidden from graph authoring"), Context.bHiddenInGraph);

	FEonformTerrainNodeDescriptor Geology;
	TestTrue(TEXT("Geology descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Geology, Geology));
	TestTrue(TEXT("Geology is hidden from graph authoring"), Geology.bHiddenInGraph);

	FEonformTerrainNodeDescriptor Masks;
	TestTrue(TEXT("ProcessMasks descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::ProcessMasks, Masks));
	TestTrue(TEXT("ProcessMasks is hidden from graph authoring"), Masks.bHiddenInGraph);

	TArray<FEonformTerrainNodeDescriptor> AllDescriptors;
	FEonformTerrainNodeDescriptorRegistry::GetAll(AllDescriptors);
	TestTrue(TEXT("Descriptor registry contains public and internal nodes"), AllDescriptors.Num() >= 10);
	return true;
}

#endif
