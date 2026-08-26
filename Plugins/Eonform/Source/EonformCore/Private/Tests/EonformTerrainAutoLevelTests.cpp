#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainAutoLevelDescriptorTest,
	"Eonform.Core.Graph.AutoLevelDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainAutoLevelDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Autolevel descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::AutoLevel, Descriptor));
	TestEqual(TEXT("Autolevel display name"), Descriptor.DisplayName, FString(TEXT("Autolevel")));
	TestEqual(TEXT("Autolevel category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Autolevel has one input"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Autolevel has one output"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Autolevel parameter count"), Descriptor.Parameters.Num(), 0);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("Autolevel input name"), Descriptor.Inputs[0].Name, FName(TEXT("Input")));
		TestEqual(TEXT("Autolevel input is polymorphic"), Descriptor.Inputs[0].DataType, FName(TEXT("Any")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Autolevel output name is Out"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
		TestEqual(TEXT("Autolevel output is polymorphic"), Descriptor.Outputs[0].DataType, FName(TEXT("Any")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainAutoLevelTerminalTest,
	"Eonform.Core.Graph.AutoLevelTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainAutoLevelTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(1201, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 1201);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);

	FEonformTerrainNode AutoLevel;
	AutoLevel.Id = FGuid(1202, 2, 2, 2);
	AutoLevel.Type = EonformTerrainNodeTypes::AutoLevel;

	Recipe.Nodes = { Source, AutoLevel };

	FEonformTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = AutoLevel.Id;
	Connection.ToInput = TEXT("Input");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = AutoLevel.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Autolevel Out can terminate the terrain graph"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Autolevel result retains Height"), Height);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainAutoLevelMaskTest,
	"Eonform.Core.Graph.AutoLevelMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainAutoLevelMaskTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(1211, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 1211);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);

	FEonformTerrainNode Slope;
	Slope.Id = FGuid(1212, 2, 2, 2);
	Slope.Type = EonformTerrainNodeTypes::Slope;

	FEonformTerrainNode AutoLevel;
	AutoLevel.Id = FGuid(1213, 3, 3, 3);
	AutoLevel.Type = EonformTerrainNodeTypes::AutoLevel;

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(1214, 4, 4, 4);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Duration"), 4);
	Erosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("Erosion Strength"));
	Erosion.NumericParameters.Add(TEXT("Bias"), 0.0);

	Recipe.Nodes = { Source, Slope, AutoLevel, Erosion };

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
	Connect(Slope, TEXT("Mask"), AutoLevel, TEXT("Input"));
	Connect(Source, TEXT("Terrain"), Erosion, TEXT("Terrain"));
	Connect(AutoLevel, TEXT("Out"), Erosion, TEXT("Area"));
	Recipe.OutputNode = Erosion.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Autolevel can process an Area mask"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	return Result.bSuccess;
}

#endif
