#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainRegionPlanner.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainRegionPlannerCoverageTest,
	"Eonform.Core.RegionalEvaluation.RegionPlannerCoversWorldWithoutGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainRegionPlannerCoverageTest::RunTest(const FString& Parameters)
{
	TArray<FEonformTerrainRegionRequest> Requests;
	FString Error;
	const bool bBuilt = FEonformTerrainRegionPlanner::BuildRequests(
		FIntPoint(129, 65),
		FVector2d(-64000.0, -32000.0),
		FVector2d(64000.0, 32000.0),
		FIntPoint(4, 2),
		6,
		Requests,
		&Error);
	TestTrue(TEXT("Region plan builds"), bBuilt);
	if (!bBuilt)
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Correct request count"), Requests.Num(), 8);
	for (const FEonformTerrainRegionRequest& Request : Requests)
	{
		TestTrue(TEXT("Every request is valid"), Request.IsValid());
		TestEqual(TEXT("Every request preserves halo"), Request.EvaluationRegion.BorderSamples, 6);
	}

	const FEonformTerrainRegionRequest& R00 = Requests[0];
	const FEonformTerrainRegionRequest& R10 = Requests[1];
	const FEonformTerrainRegionRequest& R01 = Requests[4];
	TestEqual(TEXT("First region begins at first sample"), R00.StartSample, FIntPoint(0, 0));
	TestEqual(TEXT("Horizontal neighbours share seam sample"), R00.EndSample.X, R10.StartSample.X);
	TestEqual(TEXT("Vertical neighbours share seam sample"), R00.EndSample.Y, R01.StartSample.Y);
	TestTrue(TEXT("Horizontal neighbours share exact world seam"),
		FMath::IsNearlyEqual(R00.EvaluationRegion.WorldMaxCm.X, R10.EvaluationRegion.WorldMinCm.X, 1.e-9));
	TestTrue(TEXT("Vertical neighbours share exact world seam"),
		FMath::IsNearlyEqual(R00.EvaluationRegion.WorldMaxCm.Y, R01.EvaluationRegion.WorldMinCm.Y, 1.e-9));

	const FEonformTerrainRegionRequest& Last = Requests.Last();
	TestEqual(TEXT("Last region reaches final sample"), Last.EndSample, FIntPoint(128, 64));
	TestTrue(TEXT("Last region reaches world max X"), FMath::IsNearlyEqual(Last.EvaluationRegion.WorldMaxCm.X, 64000.0, 1.e-9));
	TestTrue(TEXT("Last region reaches world max Y"), FMath::IsNearlyEqual(Last.EvaluationRegion.WorldMaxCm.Y, 32000.0, 1.e-9));
	return true;
}

#endif
