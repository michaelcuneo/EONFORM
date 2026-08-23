#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaTerrainRecipe MakeIncrementalRecipe(double MacroStrength)
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(401, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 65);
		Source.IntegerParameters.Add(TEXT("Seed"), 77);
		Source.NumericParameters.Add(TEXT("WorldSize"), 64000.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 8000.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.0008);
		Source.IntegerParameters.Add(TEXT("Octaves"), 3);

		FGaeaTerrainNode Shape;
		Shape.Id = FGuid(402, 2, 2, 2);
		Shape.Type = GaeaTerrainNodeTypes::TerrainShape;
		Shape.IntegerParameters.Add(TEXT("Seed"), 77);
		Shape.NumericParameters.Add(TEXT("MacroStrength"), MacroStrength);

		FGaeaTerrainConnection Connection;
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
	FGaeaTerrainIncrementalEvaluationTest,
	"CodenameGaea.Core.Graph.IncrementalEvaluationReusesCleanNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainIncrementalEvaluationTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext Context;
	Context.CacheContextRevision = 1;
	FGaeaTerrainEvaluationCache Cache;

	const FGaeaTerrainEvaluationResult First = FGaeaTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(0.75), Context, Cache);
	TestTrue(TEXT("Initial incremental evaluation succeeds"), First.bSuccess);
	if (!First.bSuccess)
	{
		AddError(First.Error);
		return false;
	}
	TestEqual(TEXT("Initial evaluation computes both reachable nodes"), First.EvaluatedNodeCount, 2);
	TestEqual(TEXT("Initial evaluation has no cache hits"), First.CachedNodeCount, 0);

	const FGaeaTerrainEvaluationResult Second = FGaeaTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(0.75), Context, Cache);
	TestTrue(TEXT("Identical incremental evaluation succeeds"), Second.bSuccess);
	TestEqual(TEXT("Identical evaluation recomputes no nodes"), Second.EvaluatedNodeCount, 0);
	TestEqual(TEXT("Identical evaluation reuses both nodes"), Second.CachedNodeCount, 2);

	const FGaeaTerrainEvaluationResult Changed = FGaeaTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(1.15), Context, Cache);
	TestTrue(TEXT("Downstream parameter edit succeeds"), Changed.bSuccess);
	TestEqual(TEXT("Downstream parameter edit recomputes only the changed node"), Changed.EvaluatedNodeCount, 1);
	TestEqual(TEXT("Downstream parameter edit reuses the unchanged source"), Changed.CachedNodeCount, 1);

	Context.CacheContextRevision = 2;
	const FGaeaTerrainEvaluationResult ContextChanged = FGaeaTerrainEvaluator::EvaluateIncremental(
		MakeIncrementalRecipe(1.15), Context, Cache);
	TestTrue(TEXT("Context revision change succeeds"), ContextChanged.bSuccess);
	TestEqual(TEXT("Context revision invalidates both nodes"), ContextChanged.EvaluatedNodeCount, 2);
	TestEqual(TEXT("Context revision change has no cache hits"), ContextChanged.CachedNodeCount, 0);
	return true;
}

#endif
