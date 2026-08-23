#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformNodes.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainMountainProcessTest,
	"CodenameGaea.Core.Graph.TerrainMountainProcessDetail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainMountainProcessTest::RunTest(const FString& Parameters)
{
	RegisterGaeaTerrainLandformNodes();

	FGaeaTerrainNode Mountain;
	Mountain.Id = FGuid(0x4D50524F, 0x43455353, 0x44544C31, 0x00000001);
	Mountain.Type = GaeaTerrainNodeTypes::Mountain;
	Mountain.NameParameters.Add(TEXT("Style"), TEXT("Alpine"));
	Mountain.NameParameters.Add(TEXT("Bulk"), TEXT("Medium"));
	Mountain.NumericParameters.Add(TEXT("Scale"), 1.0);
	Mountain.NumericParameters.Add(TEXT("Height"), 0.92);
	Mountain.IntegerParameters.Add(TEXT("Seed"), 4451);
	Mountain.BoolParameters.Add(TEXT("ReduceDetails"), false);

	FGaeaTerrainRecipe Recipe;
	Recipe.OutputNode = Mountain.Id;
	Recipe.Nodes.Add(Mountain);

	FGaeaTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(10000.0, 10000.0, 3000.0, 0.0);
	Context.TargetResolution = FIntPoint(257, 257);

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Detailed Mountain evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* Wear = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Wear);
	const FGaeaScalarField* Flow = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Flow);
	const FGaeaScalarField* Deposits = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Deposits);
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
