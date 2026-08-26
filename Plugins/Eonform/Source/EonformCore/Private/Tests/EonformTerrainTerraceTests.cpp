#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformTerraceTests
{
	void Connect(FEonformTerrainRecipe& Recipe, const FGuid& FromNode, FName FromOutput, const FGuid& ToNode, FName ToInput)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = FromNode;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = ToNode;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainTerraceDescriptorTest,
	"Eonform.Core.Graph.TerraceDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainTerraceDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Terraces descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Terrace, Descriptor));
	TestEqual(TEXT("Terraces display name"), Descriptor.DisplayName, FString(TEXT("Terraces")));
	TestEqual(TEXT("Terraces category"), Descriptor.Category, FString(TEXT("Surface")));
	TestEqual(TEXT("Terraces input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Terraces output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Terraces parameter count"), Descriptor.Parameters.Num(), 5);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Terraces output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainTerraceTerminalTest,
	"Eonform.Core.Graph.TerraceTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainTerraceTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xD1000001, 0xD1000002, 0xD1000003, 0xD1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 4921);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode Terrace;
	Terrace.Id = FGuid(0xD2000001, 0xD2000002, 0xD2000003, 0xD2000004);
	Terrace.Type = EonformTerrainNodeTypes::Terrace;
	Terrace.IntegerParameters.Add(TEXT("Terraces"), 10);
	Terrace.NumericParameters.Add(TEXT("Uniformity"), 0.8);
	Terrace.NumericParameters.Add(TEXT("Steepness"), 0.65);
	Terrace.NumericParameters.Add(TEXT("Intensity"), 0.9);
	Terrace.IntegerParameters.Add(TEXT("Seed"), 1287);

	Recipe.Nodes = { Source, Terrace };
	EonformTerraceTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Terrace.Id, TEXT("Terrain"));
	Recipe.OutputNode = Terrace.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Terraces terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Terraces result has Height"), Height);
	return Height != nullptr;
}

#endif
