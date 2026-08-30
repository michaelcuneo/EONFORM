#if WITH_DEV_AUTOMATION_TESTS

#include "EonformBlurNode.h"
#include "EonformPerlinNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeRegionalBlurRecipe(bool bChainTwoBlurs)
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Perlin;
		Perlin.Id = FGuid(0x7B100001, 0x7B100002, 0x7B100003, 0x7B100004);
		Perlin.Type = EonformTerrainNodeTypes::PerlinNoise;
		Perlin.NumericParameters.Add(TEXT("Scale"), 0.61);
		Perlin.IntegerParameters.Add(TEXT("Octaves"), 5);
		Perlin.NumericParameters.Add(TEXT("Gain"), 0.48);
		Perlin.IntegerParameters.Add(TEXT("Seed"), 8217);
		Perlin.NameParameters.Add(TEXT("WarpType"), TEXT("None"));

		FEonformTerrainNode Blur0;
		Blur0.Id = FGuid(0x7B200001, 0x7B200002, 0x7B200003, 0x7B200004);
		Blur0.Type = EonformTerrainNodeTypes::Blur;
		Blur0.NumericParameters.Add(TEXT("Radius"), 0.8);

		FEonformTerrainConnection C0;
		C0.FromNode = Perlin.Id;
		C0.FromOutput = TEXT("Out");
		C0.ToNode = Blur0.Id;
		C0.ToInput = TEXT("Input");

		Recipe.Nodes = { Perlin, Blur0 };
		Recipe.Connections = { C0 };
		Recipe.OutputNode = Blur0.Id;

		if (bChainTwoBlurs)
		{
			FEonformTerrainNode Blur1;
			Blur1.Id = FGuid(0x7B300001, 0x7B300002, 0x7B300003, 0x7B300004);
			Blur1.Type = EonformTerrainNodeTypes::Blur;
			Blur1.NumericParameters.Add(TEXT("Radius"), 0.4);

			FEonformTerrainConnection C1;
			C1.FromNode = Blur0.Id;
			C1.FromOutput = TEXT("Out");
			C1.ToNode = Blur1.Id;
			C1.ToInput = TEXT("Input");

			Recipe.Nodes.Add(Blur1);
			Recipe.Connections.Add(C1);
			Recipe.OutputNode = Blur1.Id;
		}

		return Recipe;
	}

	FEonformTerrainEvaluationContext FullBlurContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(129, 129);
		Context.ReferenceResolution = FIntPoint(129, 129);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(1280.0, 1280.0, 1200.0, 0.0);
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalBlurSupportMarginTest,
	"Eonform.Core.RegionalEvaluation.BlurAccumulatesDependencyMargins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalBlurSupportMarginTest::RunTest(const FString& Parameters)
{
	const FEonformTerrainRecipe Recipe = MakeRegionalBlurRecipe(true);
	const FEonformTerrainRegionalSupportReport Support =
		FEonformTerrainRegionalSupport::Analyze(Recipe, FIntPoint(129, 129));

	TestTrue(TEXT("Chained Blur graph is admitted for regional evaluation"), Support.bSupported);
	if (!Support.bSupported)
	{
		AddError(Support.Describe());
		return false;
	}

	// Radius 0.8 -> 2 samples and Radius 0.4 -> 1 sample at 129x129.
	// Sequential neighbourhood dependencies must add, not max.
	TestEqual(TEXT("Chained Blur dependency margin is cumulative"), Support.RequiredBorderSamples, 3);
	return Support.RequiredBorderSamples == 3;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalBlurMatchesFullWorldTest,
	"Eonform.Core.RegionalEvaluation.BlurMatchesFullWorldAndSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalBlurMatchesFullWorldTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	RegisterEonformBlurNode();

	const FEonformTerrainRecipe Recipe = MakeRegionalBlurRecipe(true);
	const FEonformTerrainRegionalSupportReport Support =
		FEonformTerrainRegionalSupport::Analyze(Recipe, FIntPoint(129, 129));
	TestTrue(TEXT("Blur graph is region-safe"), Support.bSupported);
	if (!Support.bSupported)
	{
		AddError(Support.Describe());
		return false;
	}

	const FEonformTerrainEvaluationContext Full = FullBlurContext();
	const FEonformTerrainEvaluationResult FullResult = FEonformTerrainEvaluator::Evaluate(Recipe, Full);
	TestTrue(TEXT("Full Blur graph evaluates"), FullResult.bSuccess);
	if (!FullResult.bSuccess)
	{
		AddError(FullResult.Error);
		return false;
	}

	const FEonformScalarField* FullHeight = FullResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Full Blur Height exists"), FullHeight);
	if (!FullHeight) return false;

	FEonformTerrainEvaluationContext Left = Full;
	Left.TargetResolution = FIntPoint(65, 129);
	Left.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(0, 0);
	Left.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(64, 128);
	Left.Region.BorderSamples = Support.RequiredBorderSamples;

	FEonformTerrainEvaluationContext Right = Full;
	Right.TargetResolution = FIntPoint(65, 129);
	Right.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(64, 0);
	Right.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(128, 128);
	Right.Region.BorderSamples = Support.RequiredBorderSamples;

	const FEonformTerrainEvaluationResult LeftResult = FEonformTerrainEvaluator::Evaluate(Recipe, Left);
	TestTrue(TEXT("Left Blur region evaluates"), LeftResult.bSuccess);
	if (!LeftResult.bSuccess)
	{
		AddError(LeftResult.Error);
		return false;
	}

	const FEonformTerrainEvaluationResult RightResult = FEonformTerrainEvaluator::Evaluate(Recipe, Right);
	TestTrue(TEXT("Right Blur region evaluates"), RightResult.bSuccess);
	if (!RightResult.bSuccess)
	{
		AddError(RightResult.Error);
		return false;
	}

	const FEonformScalarField* LeftHeight = LeftResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* RightHeight = RightResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Left regional Blur Height exists"), LeftHeight);
	TestNotNull(TEXT("Right regional Blur Height exists"), RightHeight);
	if (!LeftHeight || !RightHeight) return false;

	for (int32 Y = 0; Y < 129; ++Y)
	{
		for (int32 X = 0; X < 65; ++X)
		{
			const float LeftExpected = FullHeight->AtInterior(X, Y);
			const float LeftActual = LeftHeight->AtInterior(X, Y);
			if (!FMath::IsNearlyEqual(LeftExpected, LeftActual, 1.e-5f))
			{
				AddError(FString::Printf(
					TEXT("Left Blur region differs at %d,%d: full %.9f regional %.9f"),
					X, Y, LeftExpected, LeftActual));
				return false;
			}

			const float RightExpected = FullHeight->AtInterior(64 + X, Y);
			const float RightActual = RightHeight->AtInterior(X, Y);
			if (!FMath::IsNearlyEqual(RightExpected, RightActual, 1.e-5f))
			{
				AddError(FString::Printf(
					TEXT("Right Blur region differs at %d,%d: full %.9f regional %.9f"),
					X, Y, RightExpected, RightActual));
				return false;
			}
		}

		if (!FMath::IsNearlyEqual(
			LeftHeight->AtInterior(64, Y),
			RightHeight->AtInterior(0, Y),
			1.e-5f))
		{
			AddError(FString::Printf(TEXT("Blur seam differs at row %d"), Y));
			return false;
		}
	}

	return true;
}

#endif
