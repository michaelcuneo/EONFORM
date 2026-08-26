#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainCurvatureRoutingTest,
	"Eonform.Core.Graph.CurvatureRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainCurvatureRoutingTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(501, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FEonformTerrainNode Curvature;
	Curvature.Id = FGuid(502, 2, 2, 2);
	Curvature.Type = EonformTerrainNodeTypes::Curvature;
	Curvature.NumericParameters.Add(TEXT("Min"), 0.0);
	Curvature.NumericParameters.Add(TEXT("Max"), 1.0);
	Curvature.NumericParameters.Add(TEXT("Falloff"), 0.1);
	Curvature.NameParameters.Add(TEXT("CurvatureType"), TEXT("Average"));
	Curvature.BoolParameters.Add(TEXT("Invert"), false);

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(503, 3, 3, 3);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);
	Erosion.NumericParameters.Add(TEXT("Strength"), 0.5);

	Recipe.Nodes = { Source, Curvature, Erosion };

	FEonformTerrainConnection SourceToCurvature;
	SourceToCurvature.FromNode = Source.Id;
	SourceToCurvature.FromOutput = TEXT("Terrain");
	SourceToCurvature.ToNode = Curvature.Id;
	SourceToCurvature.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToCurvature);

	FEonformTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FEonformTerrainConnection CurvatureToMask;
	CurvatureToMask.FromNode = Curvature.Id;
	CurvatureToMask.FromOutput = TEXT("Mask");
	CurvatureToMask.ToNode = Erosion.Id;
	CurvatureToMask.ToInput = TEXT("Mask");
	Recipe.Connections.Add(CurvatureToMask);

	Recipe.OutputNode = Erosion.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Curvature-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Curvature-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Curvature-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
