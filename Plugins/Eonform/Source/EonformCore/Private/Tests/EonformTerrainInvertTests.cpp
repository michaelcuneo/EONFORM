#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace EonformInvertTests
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
	FEonformTerrainInvertTerminalTest,
	"Eonform.Core.Graph.InvertTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainInvertTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0x81000001, 0x81000002, 0x81000003, 0x81000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 6060);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0018);

	FEonformTerrainNode Invert;
	Invert.Id = FGuid(0x82000001, 0x82000002, 0x82000003, 0x82000004);
	Invert.Type = EonformTerrainNodeTypes::Invert;

	Recipe.Nodes = { Source, Invert };
	EonformInvertTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Invert.Id, TEXT("Input"));
	Recipe.OutputNode = Invert.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Invert terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Invert result has Height"), Height);
	return Height != nullptr;
}

#endif
