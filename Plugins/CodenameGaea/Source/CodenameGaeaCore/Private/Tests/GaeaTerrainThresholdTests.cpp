#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaThresholdTests
{
	void Connect(FGaeaTerrainRecipe& Recipe, const FGuid& FromNode, FName FromOutput, const FGuid& ToNode, FName ToInput)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = FromNode;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = ToNode;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainThresholdDescriptorTest,
	"CodenameGaea.Core.Graph.ThresholdDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainThresholdDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Threshold descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Threshold, Descriptor));
	TestEqual(TEXT("Threshold input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Threshold output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Threshold parameter count"), Descriptor.Parameters.Num(), 1);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Threshold output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	if (Descriptor.Parameters.Num() == 1)
	{
		TestEqual(TEXT("Threshold Level parameter"), Descriptor.Parameters[0].Name, FName(TEXT("Level")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainThresholdTerminalTest,
	"CodenameGaea.Core.Graph.ThresholdTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainThresholdTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x91000001, 0x91000002, 0x91000003, 0x91000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 7131);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Threshold;
	Threshold.Id = FGuid(0x92000001, 0x92000002, 0x92000003, 0x92000004);
	Threshold.Type = GaeaTerrainNodeTypes::Threshold;
	Threshold.NumericParameters.Add(TEXT("Level"), 0.5);

	Recipe.Nodes = { Source, Threshold };
	GaeaThresholdTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Threshold.Id, TEXT("Input"));
	Recipe.OutputNode = Threshold.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Threshold terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Threshold result has Height"), Height);
	return Height != nullptr;
}

#endif
