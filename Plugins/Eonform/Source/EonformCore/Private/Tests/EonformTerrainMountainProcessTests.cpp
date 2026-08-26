#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainLandformNodes.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainMountainProcessTest,
	"Eonform.Core.Graph.TerrainMountainProcessDetail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainMountainProcessTest::RunTest(const FString& Parameters)
{
	RegisterEonformTerrainLandformNodes();

	FEonformTerrainNode Mountain;
	Mountain.Id = FGuid(0x4D50524F, 0x43455353, 0x44544C31, 0x00000001);
	Mountain.Type = EonformTerrainNodeTypes::Mountain;
	Mountain.NameParameters.Add(TEXT("Style"), TEXT("Alpine"));
	Mountain.NameParameters.Add(TEXT("Bulk"), TEXT("Medium"));
	Mountain.NumericParameters.Add(TEXT("Scale"), 1.0);
	Mountain.NumericParameters.Add(TEXT("Height"), 0.92);
	Mountain.IntegerParameters.Add(TEXT("Seed"), 4451);
	Mountain.BoolParameters.Add(TEXT("ReduceDetails"), false);

	FEonformTerrainRecipe Recipe;
	Recipe.OutputNode = Mountain.Id;
	Recipe.Nodes.Add(Mountain);

	FEonformTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(10000.0, 10000.0, 3000.0, 0.0);
	Context.TargetResolution = FIntPoint(257, 257);

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Detailed Mountain evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* Wear = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Wear);
	const FEonformScalarField* Flow = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Flow);
	const FEonformScalarField* Deposits = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Deposits);
	TestNotNull(TEXT("Mountain keeps hydraulic wear"), Wear);
	TestNotNull(TEXT("Mountain keeps hydraulic flow"), Flow);
	TestNotNull(TEXT("Mountain keeps hydraulic deposits"), Deposits);
	if (!Height || !Wear || !Flow || !Deposits) return false;

	float MaxWear = 0.0f;
	float MaxFlow = 0.0f;
	float MaxDeposits = 0.0f;
	int32 WornSamples = 0;
	for (int32 I = 0; I < Height->Values.Num(); ++I)
	{
		MaxWear = FMath::Max(MaxWear, Wear->Values[I]);
		MaxFlow = FMath::Max(MaxFlow, Flow->Values[I]);
		MaxDeposits = FMath::Max(MaxDeposits, Deposits->Values[I]);
		if (Wear->Values[I] > 1.0e-5f) ++WornSamples;
	}

	TestTrue(TEXT("Mountain hydraulic pass visibly cuts terrain"), MaxWear > 0.001f);
	TestTrue(TEXT("Mountain runoff concentrates into channels"), MaxFlow > 1.0f);
	TestTrue(TEXT("Mountain transports/deposits sediment"), MaxDeposits > 0.00001f);
	TestTrue(TEXT("Mountain erosion affects a spatial network, not one cell"), WornSamples > Height->Values.Num() / 500);
	return true;
}

#endif
