#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainCombineDescriptorTest,
	"Eonform.Core.Graph.CombineDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainCombineDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Combine;
	TestTrue(TEXT("Combine descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Combine, Combine));
	TestEqual(TEXT("Combine display name"), Combine.DisplayName, FString(TEXT("Combine")));
	TestEqual(TEXT("Combine category"), Combine.Category, FString(TEXT("Utility")));
	TestEqual(TEXT("Combine has three inputs"), Combine.Inputs.Num(), 3);
	TestEqual(TEXT("Combine has one output"), Combine.Outputs.Num(), 1);
	TestEqual(TEXT("Combine parameter count"), Combine.Parameters.Num(), 4);
	if (Combine.Inputs.Num() == 3)
	{
		TestEqual(TEXT("Combine Input1 input"), Combine.Inputs[0].Name, FName(TEXT("Input1")));
		TestEqual(TEXT("Combine Input2 input"), Combine.Inputs[1].Name, FName(TEXT("Input2")));
		TestEqual(TEXT("Combine Mask input"), Combine.Inputs[2].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Combine Input1 is polymorphic"), Combine.Inputs[0].DataType, FName(TEXT("Any")));
		TestEqual(TEXT("Combine Mask is scalar"), Combine.Inputs[2].DataType, FName(TEXT("ScalarField")));
	}
	if (Combine.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Combine output"), Combine.Outputs[0].Name, FName(TEXT("Out")));
		TestEqual(TEXT("Combine output is polymorphic"), Combine.Outputs[0].DataType, FName(TEXT("Any")));
	}
	const FEonformTerrainParameterDescriptor* Mode = Combine.Parameters.FindByPredicate(
		[](const FEonformTerrainParameterDescriptor& Parameter) { return Parameter.Name == FName(TEXT("Mode")); });
	TestNotNull(TEXT("Combine Mode parameter exists"), Mode);
	if (Mode)
	{
		TestTrue(TEXT("Combine has Blend mode"), Mode->NameOptions.Contains(TEXT("Blend")));
		TestTrue(TEXT("Combine has Multiply mode"), Mode->NameOptions.Contains(TEXT("Multiply")));
		TestTrue(TEXT("Combine has Add mode"), Mode->NameOptions.Contains(TEXT("Add")));
	}
	TestNotNull(TEXT("Combine Ratio exists"), Combine.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Ratio")); }));
	TestNotNull(TEXT("Combine Output exists"), Combine.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Output")); }));
	TestNotNull(TEXT("Combine Enhance exists"), Combine.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Enhance")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainCombineMasksTest,
	"Eonform.Core.Graph.CombineMasksRouteToErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainCombineMasksTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(901, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9191);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FEonformTerrainNode Slope;
	Slope.Id = FGuid(902, 2, 2, 2);
	Slope.Type = EonformTerrainNodeTypes::Slope;
	Slope.NumericParameters.Add(TEXT("RangeMin"), 5.0);
	Slope.NumericParameters.Add(TEXT("RangeMax"), 70.0);

	FEonformTerrainNode Height;
	Height.Id = FGuid(903, 3, 3, 3);
	Height.Type = EonformTerrainNodeTypes::Height;
	Height.NumericParameters.Add(TEXT("Min"), 0.35);
	Height.NumericParameters.Add(TEXT("Max"), 1.0);

	FEonformTerrainNode Combine;
	Combine.Id = FGuid(904, 4, 4, 4);
	Combine.Type = EonformTerrainNodeTypes::Combine;
	Combine.NameParameters.Add(TEXT("Mode"), TEXT("Multiply"));
	Combine.NumericParameters.Add(TEXT("Ratio"), 1.0);

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(905, 5, 5, 5);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Duration"), 8);
	Erosion.NumericParameters.Add(TEXT("Strength"), 1.5);
	Erosion.NumericParameters.Add(TEXT("Volume"), 1.5);
	Erosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("Erosion Strength"));
	Erosion.NumericParameters.Add(TEXT("Bias"), 0.0);

	Recipe.Nodes = { Source, Slope, Height, Combine, Erosion };

	auto Connect = [&Recipe](const FEonformTerrainNode& From, FName FromOutput, const FEonformTerrainNode& To, FName ToInput)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = From.Id;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = To.Id;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	};

	Connect(Source, TEXT("Terrain"), Slope, TEXT("Terrain"));
	Connect(Source, TEXT("Terrain"), Height, TEXT("Terrain"));
	Connect(Slope, TEXT("Mask"), Combine, TEXT("Input1"));
	Connect(Height, TEXT("Mask"), Combine, TEXT("Input2"));
	Connect(Source, TEXT("Terrain"), Erosion, TEXT("Terrain"));
	Connect(Combine, TEXT("Out"), Erosion, TEXT("Area"));
	Recipe.OutputNode = Erosion.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combined mask recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}
	const FEonformScalarField* ResultHeight = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Combined mask recipe retains Height"), ResultHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainCombineTerrainsTest,
	"Eonform.Core.Graph.CombineTerrains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainCombineTerrainsTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Primary;
	Primary.Id = FGuid(911, 1, 1, 1);
	Primary.Type = EonformTerrainNodeTypes::PerlinNoise;
	Primary.IntegerParameters.Add(TEXT("Resolution"), 17);
	Primary.IntegerParameters.Add(TEXT("Seed"), 1111);
	Primary.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Primary.NumericParameters.Add(TEXT("HeightScale"), 5000.0);

	FEonformTerrainNode Secondary = Primary;
	Secondary.Id = FGuid(912, 2, 2, 2);
	Secondary.IntegerParameters[TEXT("Seed")] = 2222;

	FEonformTerrainNode Combine;
	Combine.Id = FGuid(913, 3, 3, 3);
	Combine.Type = EonformTerrainNodeTypes::Combine;
	Combine.NameParameters.Add(TEXT("Mode"), TEXT("Blend"));
	Combine.NumericParameters.Add(TEXT("Ratio"), 0.5);

	FEonformTerrainNode Thermal;
	Thermal.Id = FGuid(914, 4, 4, 4);
	Thermal.Type = EonformTerrainNodeTypes::ThermalErosion;
	Thermal.IntegerParameters.Add(TEXT("Iterations"), 1);

	Recipe.Nodes = { Primary, Secondary, Combine, Thermal };

	auto Connect = [&Recipe](const FEonformTerrainNode& From, FName FromOutput, const FEonformTerrainNode& To, FName ToInput)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = From.Id;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = To.Id;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	};

	Connect(Primary, TEXT("Terrain"), Combine, TEXT("Input1"));
	Connect(Secondary, TEXT("Terrain"), Combine, TEXT("Input2"));
	Connect(Combine, TEXT("Out"), Thermal, TEXT("Terrain"));
	Recipe.OutputNode = Thermal.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combined terrain recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}
	return true;
}

#endif
