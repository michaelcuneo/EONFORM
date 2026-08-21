#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaTerraceTests
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
	FGaeaTerrainTerraceDescriptorTest,
	"CodenameGaea.Core.Graph.TerraceDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainTerraceDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Terrace descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Terrace, Descriptor));
	TestEqual(TEXT("Terrace display name"), Descriptor.DisplayName, FString(TEXT("Terrace")));
	TestEqual(TEXT("Terrace category"), Descriptor.Category, FString(TEXT("Profile")));
	TestEqual(TEXT("Terrace input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Terrace output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Terrace parameter count"), Descriptor.Parameters.Num(), 8);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Terrace output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainTerraceTerminalTest,
	"CodenameGaea.Core.Graph.TerraceTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainTerraceTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0xD1000001, 0xD1000002, 0xD1000003, 0xD1000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 4921);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Terrace;
	Terrace.Id = FGuid(0xD2000001, 0xD2000002, 0xD2000003, 0xD2000004);
	Terrace.Type = GaeaTerrainNodeTypes::Terrace;
	Terrace.IntegerParameters.Add(TEXT("Terraces"), 10);
	Terrace.NumericParameters.Add(TEXT("Uniformity"), 0.8);
	Terrace.NumericParameters.Add(TEXT("Steepness"), 0.65);
	Terrace.NumericParameters.Add(TEXT("Intensity"), 0.9);
	Terrace.NumericParameters.Add(TEXT("SoftFalloff"), 0.15);
	Terrace.BoolParameters.Add(TEXT("Reprocess"), false);
	Terrace.NumericParameters.Add(TEXT("Process"), 1.0);
	Terrace.IntegerParameters.Add(TEXT("Seed"), 1287);

	Recipe.Nodes = { Source, Terrace };
	GaeaTerraceTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Terrace.Id, TEXT("Terrain"));
	Recipe.OutputNode = Terrace.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Terrace terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Terrace result has Height"), Height);
	return Height != nullptr;
}

#endif
