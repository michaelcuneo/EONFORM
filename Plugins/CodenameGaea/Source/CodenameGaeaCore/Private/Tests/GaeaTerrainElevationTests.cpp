#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainElevationRoutingTest,
	"CodenameGaea.Core.Graph.ElevationRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainElevationRoutingTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(601, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 6060);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Elevation;
	Elevation.Id = FGuid(602, 2, 2, 2);
	Elevation.Type = GaeaTerrainNodeTypes::Elevation;

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(603, 3, 3, 3);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);
	Erosion.NumericParameters.Add(TEXT("Strength"), 0.5);

	Recipe.Nodes = { Source, Elevation, Erosion };

	FGaeaTerrainConnection SourceToElevation;
	SourceToElevation.FromNode = Source.Id;
	SourceToElevation.FromOutput = TEXT("Terrain");
	SourceToElevation.ToNode = Elevation.Id;
	SourceToElevation.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToElevation);

	FGaeaTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FGaeaTerrainConnection ElevationToMask;
	ElevationToMask.FromNode = Elevation.Id;
	ElevationToMask.FromOutput = TEXT("Elevation");
	ElevationToMask.ToNode = Erosion.Id;
	ElevationToMask.ToInput = TEXT("Mask");
	Recipe.Connections.Add(ElevationToMask);

	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Elevation-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Elevation-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Elevation-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
