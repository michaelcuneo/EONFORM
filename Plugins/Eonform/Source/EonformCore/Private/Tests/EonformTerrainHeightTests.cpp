#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainHeightRoutingTest,
	"Eonform.Core.Graph.HeightRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainHeightRoutingTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(601, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 6060);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FEonformTerrainNode HeightSelector;
	HeightSelector.Id = FGuid(602, 2, 2, 2);
	HeightSelector.Type = EonformTerrainNodeTypes::Height;
	HeightSelector.NumericParameters.Add(TEXT("RangeMin"), 0.45);
	HeightSelector.NumericParameters.Add(TEXT("RangeMax"), 1.0);
	HeightSelector.NumericParameters.Add(TEXT("Falloff"), 0.1);

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(603, 3, 3, 3);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);
	Erosion.NumericParameters.Add(TEXT("Strength"), 0.5);

	Recipe.Nodes = { Source, HeightSelector, Erosion };

	FEonformTerrainConnection SourceToHeight;
	SourceToHeight.FromNode = Source.Id;
	SourceToHeight.FromOutput = TEXT("Terrain");
	SourceToHeight.ToNode = HeightSelector.Id;
	SourceToHeight.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToHeight);

	FEonformTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FEonformTerrainConnection HeightToMask;
	HeightToMask.FromNode = HeightSelector.Id;
	HeightToMask.FromOutput = TEXT("Mask");
	HeightToMask.ToNode = Erosion.Id;
	HeightToMask.ToInput = TEXT("Mask");
	Recipe.Connections.Add(HeightToMask);

	Recipe.OutputNode = Erosion.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Height-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Height-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Height-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
