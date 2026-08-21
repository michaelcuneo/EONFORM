#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainCombineDescriptorTest,
	"CodenameGaea.Core.Graph.CombineDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainCombineDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Combine;
	TestTrue(TEXT("Combine descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Combine, Combine));
	TestEqual(TEXT("Combine display name"), Combine.DisplayName, FString(TEXT("Combine")));
	TestEqual(TEXT("Combine category"), Combine.Category, FString(TEXT("Adjustments")));
	TestEqual(TEXT("Combine has three inputs"), Combine.Inputs.Num(), 3);
	TestEqual(TEXT("Combine has two outputs"), Combine.Outputs.Num(), 2);
	TestEqual(TEXT("Combine parameter count"), Combine.Parameters.Num(), 8);
	if (Combine.Inputs.Num() == 3)
	{
		TestEqual(TEXT("Combine Primary input"), Combine.Inputs[0].Name, FName(TEXT("Primary")));
		TestEqual(TEXT("Combine Secondary input"), Combine.Inputs[1].Name, FName(TEXT("Secondary")));
		TestEqual(TEXT("Combine Mask input"), Combine.Inputs[2].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Combine Primary is polymorphic"), Combine.Inputs[0].DataType, FName(TEXT("Any")));
		TestEqual(TEXT("Combine Mask is scalar"), Combine.Inputs[2].DataType, FName(TEXT("ScalarField")));
	}
	if (Combine.Outputs.Num() == 2)
	{
		TestEqual(TEXT("Combine output"), Combine.Outputs[0].Name, FName(TEXT("Out")));
		TestEqual(TEXT("Combine output is polymorphic"), Combine.Outputs[0].DataType, FName(TEXT("Any")));
		TestEqual(TEXT("Combine separation output"), Combine.Outputs[1].Name, FName(TEXT("Separation")));
		TestEqual(TEXT("Combine separation output is scalar"), Combine.Outputs[1].DataType, FName(TEXT("ScalarField")));
	}
	const FGaeaTerrainParameterDescriptor* Method = Combine.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter) { return Parameter.Name == FName(TEXT("Method")); });
	TestNotNull(TEXT("Combine Method parameter exists"), Method);
	if (Method)
	{
		TestEqual(TEXT("Combine method count"), Method->NameOptions.Num(), 13);
		TestTrue(TEXT("Combine has Insert method"), Method->NameOptions.Contains(TEXT("Insert")));
		TestTrue(TEXT("Combine has Embed method"), Method->NameOptions.Contains(TEXT("Embed")));
	}
	TestNotNull(TEXT("Combine Ratio exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Ratio")); }));
	TestNotNull(TEXT("Combine Swap Inputs exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("SwapInputs")); }));
	TestNotNull(TEXT("Combine Separation Mask exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("SeparationMask")); }));
	TestNotNull(TEXT("Combine Clamp Output exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("ClampOutput")); }));
	TestNotNull(TEXT("Combine Abs exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Abs")); }));
	TestNotNull(TEXT("Combine Threshold exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Threshold")); }));
	TestNotNull(TEXT("Combine Flatten exists"), Combine.Parameters.FindByPredicate([](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Flatten")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainCombineMasksTest,
	"CodenameGaea.Core.Graph.CombineMasksRouteToErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainCombineMasksTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(901, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9191);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Slope;
	Slope.Id = FGuid(902, 2, 2, 2);
	Slope.Type = GaeaTerrainNodeTypes::Slope;
	Slope.NumericParameters.Add(TEXT("Min"), 5.0);
	Slope.NumericParameters.Add(TEXT("Max"), 70.0);

	FGaeaTerrainNode Height;
	Height.Id = FGuid(903, 3, 3, 3);
	Height.Type = GaeaTerrainNodeTypes::Height;
	Height.NumericParameters.Add(TEXT("Min"), 0.35);
	Height.NumericParameters.Add(TEXT("Max"), 1.0);
	Height.BoolParameters.Add(TEXT("Normalized"), true);

	FGaeaTerrainNode Combine;
	Combine.Id = FGuid(904, 4, 4, 4);
	Combine.Type = GaeaTerrainNodeTypes::Combine;
	Combine.NameParameters.Add(TEXT("Method"), TEXT("Multiply"));
	Combine.NumericParameters.Add(TEXT("Ratio"), 1.0);
	Combine.BoolParameters.Add(TEXT("SeparationMask"), true);

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(905, 5, 5, 5);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);
	Erosion.NumericParameters.Add(TEXT("Strength"), 0.5);

	Recipe.Nodes = { Source, Slope, Height, Combine, Erosion };

	auto Connect = [&Recipe](const FGaeaTerrainNode& From, FName FromOutput, const FGaeaTerrainNode& To, FName ToInput)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From.Id;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = To.Id;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	};

	Connect(Source, TEXT("Terrain"), Slope, TEXT("Terrain"));
	Connect(Source, TEXT("Terrain"), Height, TEXT("Terrain"));
	Connect(Slope, TEXT("Mask"), Combine, TEXT("Primary"));
	Connect(Height, TEXT("Mask"), Combine, TEXT("Secondary"));
	Connect(Source, TEXT("Terrain"), Erosion, TEXT("Terrain"));
	Connect(Combine, TEXT("Out"), Erosion, TEXT("Mask"));
	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combined mask recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}
	const FGaeaScalarField* ResultHeight = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Combined mask recipe retains Height"), ResultHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainCombineTerrainsTest,
	"CodenameGaea.Core.Graph.CombineTerrains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainCombineTerrainsTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Primary;
	Primary.Id = FGuid(911, 1, 1, 1);
	Primary.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Primary.IntegerParameters.Add(TEXT("Resolution"), 17);
	Primary.IntegerParameters.Add(TEXT("Seed"), 1111);
	Primary.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Primary.NumericParameters.Add(TEXT("HeightScale"), 5000.0);

	FGaeaTerrainNode Secondary = Primary;
	Secondary.Id = FGuid(912, 2, 2, 2);
	Secondary.IntegerParameters[TEXT("Seed")] = 2222;

	FGaeaTerrainNode Combine;
	Combine.Id = FGuid(913, 3, 3, 3);
	Combine.Type = GaeaTerrainNodeTypes::Combine;
	Combine.NameParameters.Add(TEXT("Method"), TEXT("Blend"));
	Combine.NumericParameters.Add(TEXT("Ratio"), 0.5);

	FGaeaTerrainNode Thermal;
	Thermal.Id = FGuid(914, 4, 4, 4);
	Thermal.Type = GaeaTerrainNodeTypes::ThermalErosion;
	Thermal.IntegerParameters.Add(TEXT("Iterations"), 1);

	Recipe.Nodes = { Primary, Secondary, Combine, Thermal };

	auto Connect = [&Recipe](const FGaeaTerrainNode& From, FName FromOutput, const FGaeaTerrainNode& To, FName ToInput)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From.Id;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = To.Id;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	};

	Connect(Primary, TEXT("Terrain"), Combine, TEXT("Primary"));
	Connect(Secondary, TEXT("Terrain"), Combine, TEXT("Secondary"));
	Connect(Combine, TEXT("Out"), Thermal, TEXT("Terrain"));
	Recipe.OutputNode = Thermal.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combined terrain recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}
	return true;
}

#endif
