#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace EonformTypedPortTests
{
	void Connect(
		FEonformTerrainRecipe& Recipe,
		const FGuid& FromNode,
		FName FromOutput,
		const FGuid& ToNode,
		FName ToInput)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = FromNode;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = ToNode;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	}

	FEonformTerrainRecipe MakeDoubleErosionRecipe(bool bRouteFlowToArea)
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(201, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 25);
		Source.IntegerParameters.Add(TEXT("Seed"), 9182);
		Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 7000.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.0012);

		FEonformTerrainNode FirstErosion;
		FirstErosion.Id = FGuid(202, 2, 2, 2);
		FirstErosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
		FirstErosion.IntegerParameters.Add(TEXT("Duration"), 12);
		FirstErosion.NumericParameters.Add(TEXT("Strength"), 2.0);
		FirstErosion.NumericParameters.Add(TEXT("Volume"), 2.0);
		FirstErosion.NumericParameters.Add(TEXT("Downcutting"), 1.0);

		FEonformTerrainNode SecondErosion;
		SecondErosion.Id = FGuid(203, 3, 3, 3);
		SecondErosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
		SecondErosion.IntegerParameters.Add(TEXT("Duration"), 12);
		SecondErosion.NumericParameters.Add(TEXT("Strength"), 2.0);
		SecondErosion.NumericParameters.Add(TEXT("Volume"), 2.0);
		SecondErosion.NumericParameters.Add(TEXT("Downcutting"), 1.0);
		if (bRouteFlowToArea)
		{
			SecondErosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("Erosion Strength"));
			SecondErosion.NumericParameters.Add(TEXT("Bias"), 0.0);
		}

		Recipe.Nodes = { Source, FirstErosion, SecondErosion };
		Connect(Recipe, Source.Id, TEXT("Terrain"), FirstErosion.Id, TEXT("Terrain"));
		Connect(Recipe, FirstErosion.Id, TEXT("Out"), SecondErosion.Id, TEXT("Terrain"));
		if (bRouteFlowToArea)
		{
			Connect(Recipe, FirstErosion.Id, TEXT("Flow"), SecondErosion.Id, TEXT("Area"));
		}
		Recipe.OutputNode = SecondErosion.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainTypedHydraulicRoutingTest,
	"Eonform.Core.Graph.HydraulicFlowRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainTypedHydraulicRoutingTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Unmasked = FEonformTerrainEvaluator::Evaluate(
		EonformTypedPortTests::MakeDoubleErosionRecipe(false),
		Context);
	const FEonformTerrainEvaluationResult FlowMasked = FEonformTerrainEvaluator::Evaluate(
		EonformTypedPortTests::MakeDoubleErosionRecipe(true),
		Context);

	TestTrue(TEXT("Unmasked double erosion evaluates"), Unmasked.bSuccess);
	TestTrue(TEXT("Flow-routed selective erosion evaluates"), FlowMasked.bSuccess);
	if (!Unmasked.bSuccess)
	{
		AddError(Unmasked.Error);
		return false;
	}
	if (!FlowMasked.bSuccess)
	{
		AddError(FlowMasked.Error);
		return false;
	}

	const FEonformScalarField* UnmaskedHeight = Unmasked.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* MaskedHeight = FlowMasked.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Unmasked result has Height"), UnmaskedHeight);
	TestNotNull(TEXT("Flow-routed result has Height"), MaskedHeight);
	if (!UnmaskedHeight || !MaskedHeight) return false;

	TestEqual(TEXT("Routed terrain domain is preserved"), MaskedHeight->Domain, UnmaskedHeight->Domain);
	TestEqual(TEXT("Routed terrain sample count is preserved"), MaskedHeight->Values.Num(), UnmaskedHeight->Values.Num());

	bool bAnyHeightDifference = false;
	for (int32 Index = 0; Index < MaskedHeight->Values.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(MaskedHeight->Values[Index], UnmaskedHeight->Values[Index], 1.0e-7f))
		{
			bAnyHeightDifference = true;
			break;
		}
	}
	TestTrue(TEXT("Flow-driven Area Effect changes downstream erosion"), bAnyHeightDifference);
	return true;
}

#endif
