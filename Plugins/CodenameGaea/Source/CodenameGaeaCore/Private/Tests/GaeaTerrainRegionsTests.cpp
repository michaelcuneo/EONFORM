#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainRegionsRoutingTest,
	"CodenameGaea.Core.Graph.TerrainRegionsRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainRegionsRoutingTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(701, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 7070);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Regions;
	Regions.Id = FGuid(702, 2, 2, 2);
	Regions.Type = GaeaTerrainNodeTypes::TerrainRegions;

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(703, 3, 3, 3);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);
	Erosion.NumericParameters.Add(TEXT("Strength"), 0.5);

	Recipe.Nodes = { Source, Regions, Erosion };

	FGaeaTerrainConnection SourceToRegions;
	SourceToRegions.FromNode = Source.Id;
	SourceToRegions.FromOutput = TEXT("Terrain");
	SourceToRegions.ToNode = Regions.Id;
	SourceToRegions.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToRegions);

	FGaeaTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FGaeaTerrainConnection MountainToMask;
	MountainToMask.FromNode = Regions.Id;
	MountainToMask.FromOutput = TEXT("Mountain");
	MountainToMask.ToNode = Erosion.Id;
	MountainToMask.ToInput = TEXT("Mask");
	Recipe.Connections.Add(MountainToMask);

	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Terrain Regions-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Terrain Regions-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Terrain Regions-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
