#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainCurvatureRoutingTest,
	"CodenameGaea.Core.Graph.CurvatureRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainCurvatureRoutingTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(501, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Curvature;
	Curvature.Id = FGuid(502, 2, 2, 2);
	Curvature.Type = GaeaTerrainNodeTypes::Curvature;
	Curvature.NumericParameters.Add(TEXT("Min"), 0.0);
	Curvature.NumericParameters.Add(TEXT("Max"), 1.0);
	Curvature.NumericParameters.Add(TEXT("Falloff"), 0.1);
	Curvature.NameParameters.Add(TEXT("CurvatureType"), TEXT("Average"));
	Curvature.BoolParameters.Add(TEXT("Invert"), false);

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(503, 3, 3, 3);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);
	Erosion.NumericParameters.Add(TEXT("Strength"), 0.5);

	Recipe.Nodes = { Source, Curvature, Erosion };

	FGaeaTerrainConnection SourceToCurvature;
	SourceToCurvature.FromNode = Source.Id;
	SourceToCurvature.FromOutput = TEXT("Terrain");
	SourceToCurvature.ToNode = Curvature.Id;
	SourceToCurvature.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToCurvature);

	FGaeaTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FGaeaTerrainConnection CurvatureToMask;
	CurvatureToMask.FromNode = Curvature.Id;
	CurvatureToMask.FromOutput = TEXT("Mask");
	CurvatureToMask.ToNode = Erosion.Id;
	CurvatureToMask.ToInput = TEXT("Mask");
	Recipe.Connections.Add(CurvatureToMask);

	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Curvature-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Curvature-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Curvature-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
