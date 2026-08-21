#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace GaeaFlipTests
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
	FGaeaTerrainFlipTerminalTest,
	"CodenameGaea.Core.Graph.FlipTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainFlipTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Flip;
	Flip.Id = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
	Flip.Type = GaeaTerrainNodeTypes::Flip;
	Flip.NameParameters.Add(TEXT("Direction"), TEXT("Horizontal"));

	Recipe.Nodes = { Source, Flip };
	GaeaFlipTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Flip.Id, TEXT("Terrain"));
	Recipe.OutputNode = Flip.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Flip terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Flip result has Height"), Height);
	return Height != nullptr;
}

#endif
