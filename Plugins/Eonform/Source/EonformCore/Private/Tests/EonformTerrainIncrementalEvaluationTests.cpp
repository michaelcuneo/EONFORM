#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeIncrementalRecipe(double MacroStrength)
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(401, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 65);
		Source.IntegerParameters.Add(TEXT("Seed"), 77);
		Source.NumericParameters.Add(TEXT("WorldSize"), 64000.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 8000.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.0008);
		Source.IntegerParameters.Add(TEXT("Octaves"), 3);

		FEonformTerrainNode Shape;
		Shape.Id = FGuid(402, 2, 2, 2);
		Shape.Type = EonformTerrainNodeTypes::TerrainShape;
		Shape.IntegerParameters.Add(TEXT("Seed"), 77);
		Shape.NumericParameters.Add(TEXT("MacroStrength"), MacroStrength);

		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Shape.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Shape };
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Shape.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainIncrementalEvaluationTest,
	"Eonform.Core.Graph.IncrementalEvaluationReusesCleanNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainIncrementalEvaluationTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext Context;
	Context.CacheContextRevision = 1;
	FEonformTerrainEvaluationCache Cache;

	const FEonformTerrainEvaluationResult First = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(0.75), Context, Cache);
	TestTrue(TEXT("Initial incremental evaluation succeeds"), First.bSuccess);
	if (!First.bSuccess)
	{
		AddError(First.Error);
		return false;
	}
	TestEqual(TEXT("Initial evaluation computes both reachable nodes"), First.EvaluatedNodeCount, 2);
	TestEqual(TEXT("Initial evaluation has no cache hits"), First.CachedNodeCount, 0);

	const FEonformTerrainEvaluationResult Second = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(0.75), Context, Cache);
	TestTrue(TEXT("Identical incremental evaluation succeeds"), Second.bSuccess);
	TestEqual(TEXT("Identical evaluation recomputes no nodes"), Second.EvaluatedNodeCount, 0);
	TestEqual(TEXT("Identical evaluation reuses both nodes"), Second.CachedNodeCount, 2);

	const FEonformTerrainEvaluationResult Changed = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(1.15), Context, Cache);
	TestTrue(TEXT("Downstream parameter edit succeeds"), Changed.bSuccess);
	TestEqual(TEXT("Downstream parameter edit recomputes only the changed node"), Changed.EvaluatedNodeCount, 1);
	TestEqual(TEXT("Downstream parameter edit reuses the unchanged source"), Changed.CachedNodeCount, 1);

	Context.CacheContextRevision = 2;
	const FEonformTerrainEvaluationResult ContextChanged = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(1.15), Context, Cache);
	TestTrue(TEXT("Context revision change succeeds"), ContextChanged.bSuccess);
	TestEqual(TEXT("Context revision invalidates both nodes"), ContextChanged.EvaluatedNodeCount, 2);
	TestEqual(TEXT("Context revision change has no cache hits"), ContextChanged.CachedNodeCount, 0);
	return true;
}

#endif
