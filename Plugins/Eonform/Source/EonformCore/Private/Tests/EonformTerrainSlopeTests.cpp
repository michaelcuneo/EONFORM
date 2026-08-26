#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeSlopeMaskedErosionRecipe(bool bUseSlopeMask)
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(0x31000001, 0x31000002, 0x31000003, 0x31000004);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 33);
		Source.IntegerParameters.Add(TEXT("Seed"), 101);
		Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

		FEonformTerrainNode Slope;
		Slope.Id = FGuid(0x32000001, 0x32000002, 0x32000003, 0x32000004);
		Slope.Type = EonformTerrainNodeTypes::Slope;
		Slope.NumericParameters.Add(TEXT("RangeMin"), 8.0);
		Slope.NumericParameters.Add(TEXT("RangeMax"), 28.0);
		Slope.NumericParameters.Add(TEXT("Falloff"), 8.0);

		FEonformTerrainNode Erosion;
		Erosion.Id = FGuid(0x33000001, 0x33000002, 0x33000003, 0x33000004);
		Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Duration"), 12);
		Erosion.NumericParameters.Add(TEXT("Strength"), 2.0);
		Erosion.NumericParameters.Add(TEXT("Volume"), 1.5);
		if (bUseSlopeMask)
		{
			Erosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("Erosion Strength"));
			Erosion.NumericParameters.Add(TEXT("Bias"), 0.0);
		}

		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Slope);
		Recipe.Nodes.Add(Erosion);

		FEonformTerrainConnection TerrainToSlope;
		TerrainToSlope.FromNode = Source.Id;
		TerrainToSlope.FromOutput = TEXT("Terrain");
		TerrainToSlope.ToNode = Slope.Id;
		TerrainToSlope.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(TerrainToSlope);

		FEonformTerrainConnection TerrainToErosion;
		TerrainToErosion.FromNode = Source.Id;
		TerrainToErosion.FromOutput = TEXT("Terrain");
		TerrainToErosion.ToNode = Erosion.Id;
		TerrainToErosion.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(TerrainToErosion);

		if (bUseSlopeMask)
		{
			FEonformTerrainConnection MaskConnection;
			MaskConnection.FromNode = Slope.Id;
			MaskConnection.FromOutput = TEXT("Mask");
			MaskConnection.ToNode = Erosion.Id;
			MaskConnection.ToInput = TEXT("Area");
			Recipe.Connections.Add(MaskConnection);
		}

		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainSlopeMaskRoutingTest,
	"Eonform.Core.Graph.SlopeMaskRoutesToErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainSlopeMaskRoutingTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Unmasked = FEonformTerrainEvaluator::Evaluate(MakeSlopeMaskedErosionRecipe(false), Context);
	const FEonformTerrainEvaluationResult Masked = FEonformTerrainEvaluator::Evaluate(MakeSlopeMaskedErosionRecipe(true), Context);

	TestTrue(TEXT("Unmasked erosion evaluates"), Unmasked.bSuccess);
	TestTrue(TEXT("Slope-masked selective erosion evaluates"), Masked.bSuccess);
	if (!Unmasked.bSuccess || !Masked.bSuccess)
	{
		if (!Unmasked.bSuccess) AddError(Unmasked.Error);
		if (!Masked.bSuccess) AddError(Masked.Error);
		return false;
	}

	const FEonformScalarField* UnmaskedHeight = Unmasked.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* MaskedHeight = Masked.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Unmasked result has Height"), UnmaskedHeight);
	TestNotNull(TEXT("Masked result has Height"), MaskedHeight);
	if (!UnmaskedHeight || !MaskedHeight) return false;

	TestEqual(TEXT("Masked and unmasked domains match"), MaskedHeight->Domain, UnmaskedHeight->Domain);
	TestEqual(TEXT("Masked and unmasked sample counts match"), MaskedHeight->Values.Num(), UnmaskedHeight->Values.Num());

	double Difference = 0.0;
	for (int32 Index = 0; Index < MaskedHeight->Values.Num(); ++Index)
	{
		Difference += FMath::Abs(MaskedHeight->Values[Index] - UnmaskedHeight->Values[Index]);
	}
	TestTrue(TEXT("Slope-driven Erosion Strength changes downstream erosion"), Difference > 0.0001);
	return true;
}

#endif
