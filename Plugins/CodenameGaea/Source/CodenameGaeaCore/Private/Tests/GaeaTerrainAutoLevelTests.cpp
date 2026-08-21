#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainAutoLevelDescriptorTest,
	"CodenameGaea.Core.Graph.AutoLevelDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainAutoLevelDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("AutoLevel descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::AutoLevel, Descriptor));
	TestEqual(TEXT("AutoLevel display name"), Descriptor.DisplayName, FString(TEXT("AutoLevel")));
	TestEqual(TEXT("AutoLevel category"), Descriptor.Category, FString(TEXT("Adjustments")));
	TestEqual(TEXT("AutoLevel has one input"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("AutoLevel has one output"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("AutoLevel parameter count"), Descriptor.Parameters.Num(), 12);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("AutoLevel input name"), Descriptor.Inputs[0].Name, FName(TEXT("Input")));
		TestEqual(TEXT("AutoLevel input is polymorphic"), Descriptor.Inputs[0].DataType, FName(TEXT("Any")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("AutoLevel output name is Out"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
		TestEqual(TEXT("AutoLevel output is polymorphic"), Descriptor.Outputs[0].DataType, FName(TEXT("Any")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainAutoLevelTerminalTest,
	"CodenameGaea.Core.Graph.AutoLevelTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainAutoLevelTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(1201, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 1201);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);

	FGaeaTerrainNode AutoLevel;
	AutoLevel.Id = FGuid(1202, 2, 2, 2);
	AutoLevel.Type = GaeaTerrainNodeTypes::AutoLevel;
	AutoLevel.BoolParameters.Add(TEXT("ApplyAutoLevel"), true);
	AutoLevel.NumericParameters.Add(TEXT("AutoLevelStrength"), 1.0);

	Recipe.Nodes = { Source, AutoLevel };

	FGaeaTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = AutoLevel.Id;
	Connection.ToInput = TEXT("Input");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = AutoLevel.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("AutoLevel Out can terminate the terrain graph"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("AutoLevel result retains Height"), Height);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainAutoLevelMaskTest,
	"CodenameGaea.Core.Graph.AutoLevelMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainAutoLevelMaskTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(1211, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 1211);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);

	FGaeaTerrainNode Slope;
	Slope.Id = FGuid(1212, 2, 2, 2);
	Slope.Type = GaeaTerrainNodeTypes::Slope;

	FGaeaTerrainNode AutoLevel;
	AutoLevel.Id = FGuid(1213, 3, 3, 3);
	AutoLevel.Type = GaeaTerrainNodeTypes::AutoLevel;

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(1214, 4, 4, 4);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 1);

	Recipe.Nodes = { Source, Slope, AutoLevel, Erosion };

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
	Connect(Slope, TEXT("Mask"), AutoLevel, TEXT("Input"));
	Connect(Source, TEXT("Terrain"), Erosion, TEXT("Terrain"));
	Connect(AutoLevel, TEXT("Out"), Erosion, TEXT("Mask"));
	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("AutoLevel can process a mask"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	return Result.bSuccess;
}

#endif
