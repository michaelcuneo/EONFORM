#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaSineTests
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
	FGaeaTerrainSineDescriptorTest,
	"CodenameGaea.Core.Graph.SineDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainSineDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Sine descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Sine, Descriptor));
	TestEqual(TEXT("Sine display name"), Descriptor.DisplayName, FString(TEXT("Sine")));
	TestEqual(TEXT("Sine category"), Descriptor.Category, FString(TEXT("Adjustments")));
	TestEqual(TEXT("Sine input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Sine output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Sine parameter count"), Descriptor.Parameters.Num(), 1);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Sine output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	if (Descriptor.Parameters.Num() == 1)
	{
		TestEqual(TEXT("Sine Amount parameter"), Descriptor.Parameters[0].Name, FName(TEXT("Amount")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainSineTerminalTest,
	"CodenameGaea.Core.Graph.SineTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainSineTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x83000001, 0x83000002, 0x83000003, 0x83000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 7261);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Sine;
	Sine.Id = FGuid(0x84000001, 0x84000002, 0x84000003, 0x84000004);
	Sine.Type = GaeaTerrainNodeTypes::Sine;
	Sine.NumericParameters.Add(TEXT("Amount"), 0.5);

	Recipe.Nodes = { Source, Sine };
	GaeaSineTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Sine.Id, TEXT("Terrain"));
	Recipe.OutputNode = Sine.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Sine terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Sine result has Height"), Height);
	return Height != nullptr;
}

#endif
