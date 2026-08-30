#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainLandformNodes.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeRegionalMountainRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Mountain;
		Mountain.Id = FGuid(9720, 20, 20, 20);
		Mountain.Type = TEXT("Mountain");
		Mountain.NumericParameters.Add(TEXT("Scale"), 0.56);
		Mountain.NumericParameters.Add(TEXT("Height"), 1.18);
		Mountain.NameParameters.Add(TEXT("Style"), TEXT("Basic"));
		Mountain.NameParameters.Add(TEXT("Bulk"), TEXT("Medium"));
		Mountain.IntegerParameters.Add(TEXT("Seed"), 41273);
		Mountain.NumericParameters.Add(TEXT("X"), 0.47);
		Mountain.NumericParameters.Add(TEXT("Y"), 0.54);
		Recipe.Nodes.Add(Mountain);
		Recipe.OutputNode = Mountain.Id;
		return Recipe;
	}

	FEonformTerrainEvaluationContext MountainFullContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(65, 65);
		Context.ReferenceResolution = FIntPoint(65, 65);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(640.0, 640.0, 1200.0, 0.0);
		return Context;
	}

	bool CompareRegionToFull(
		FAutomationTestBase& Test,
		const FEonformScalarField& FullHeight,
		const FEonformScalarField& RegionHeight,
		int32 StartX)
	{
		for (int32 Y = 0; Y < RegionHeight.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < RegionHeight.Domain.Dimensions.X; ++X)
			{
				const float Expected = FullHeight.AtInterior(StartX + X, Y);
				const float Actual = RegionHeight.AtInterior(X, Y);
				if (!FMath::IsNearlyEqual(Expected, Actual, 1.e-5f))
				{
					Test.AddError(FString::Printf(
						TEXT("Regional Mountain differs at full %d,%d / local %d,%d: full %.9f regional %.9f"),
						StartX + X, Y, X, Y, Expected, Actual));
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformMountainRegionalSupportTest,
	"Eonform.Core.RegionalEvaluation.MountainUsesStreamedRidgeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainRegionalSupportTest::RunTest(const FString& Parameters)
{
	const FEonformTerrainRecipe Recipe = MakeRegionalMountainRecipe();
	const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Recipe);
	TestTrue(TEXT("Basic/Medium Mountain is admitted for regional evaluation"), Support.bSupported);
	TestEqual(TEXT("Streamed Mountain core requires no guessed regional halo"), Support.RequiredBorderSamples, 0);
	return Support.bSupported;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformMountainRegionalEquivalenceTest,
	"Eonform.Core.RegionalEvaluation.MountainCoreMatchesFullWorldAndSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainRegionalEquivalenceTest::RunTest(const FString& Parameters)
{
	RegisterEonformTerrainLandformNodes();

	const FEonformTerrainRecipe Recipe = MakeRegionalMountainRecipe();
	FEonformTerrainEvaluationContext Full = MountainFullContext();
	const FEonformTerrainEvaluationResult FullResult = FEonformTerrainEvaluator::Evaluate(Recipe, Full);
	TestTrue(TEXT("Full Mountain evaluates"), FullResult.bSuccess);
	if (!FullResult.bSuccess)
	{
		AddError(FullResult.Error);
		return false;
	}

	const FEonformScalarField* FullHeight = FullResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Full Mountain Height exists"), FullHeight);
	if (!FullHeight) return false;

	FEonformTerrainEvaluationContext Left = Full;
	Left.TargetResolution = FIntPoint(33, 65);
	Left.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(0, 0);
	Left.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(32, 64);
	Left.Region.BorderSamples = 0;

	FEonformTerrainEvaluationContext Right = Full;
	Right.TargetResolution = FIntPoint(33, 65);
	Right.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(32, 0);
	Right.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(64, 64);
	Right.Region.BorderSamples = 0;

	// Adjacent regions share the same exact Ridge global reductions, matching the
	// generation-plan contract used by the runtime materializer.
	Right.GlobalSummaryCache = Left.GlobalSummaryCache;

	const FEonformTerrainEvaluationResult LeftResult = FEonformTerrainEvaluator::Evaluate(Recipe, Left);
	const FEonformTerrainEvaluationResult RightResult = FEonformTerrainEvaluator::Evaluate(Recipe, Right);
	TestTrue(TEXT("Left Mountain region evaluates"), LeftResult.bSuccess);
	TestTrue(TEXT("Right Mountain region evaluates"), RightResult.bSuccess);
	if (!LeftResult.bSuccess || !RightResult.bSuccess)
	{
		if (!LeftResult.bSuccess) AddError(LeftResult.Error);
		if (!RightResult.bSuccess) AddError(RightResult.Error);
		return false;
	}

	const FEonformScalarField* LeftHeight = LeftResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* RightHeight = RightResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Left regional Mountain Height exists"), LeftHeight);
	TestNotNull(TEXT("Right regional Mountain Height exists"), RightHeight);
	if (!LeftHeight || !RightHeight) return false;

	if (!CompareRegionToFull(*this, *FullHeight, *LeftHeight, 0)) return false;
	if (!CompareRegionToFull(*this, *FullHeight, *RightHeight, 32)) return false;

	for (int32 Y = 0; Y < 65; ++Y)
	{
		const float LeftSeam = LeftHeight->AtInterior(32, Y);
		const float RightSeam = RightHeight->AtInterior(0, Y);
		if (!FMath::IsNearlyEqual(LeftSeam, RightSeam, 1.e-5f))
		{
			AddError(FString::Printf(
				TEXT("Mountain seam differs at row %d: left %.9f right %.9f"),
				Y, LeftSeam, RightSeam));
			return false;
		}
	}

	return true;
}

#endif
