#if WITH_DEV_AUTOMATION_TESTS

#include "EonformClampNode.h"
#include "EonformCombineNode.h"
#include "EonformConstantNode.h"
#include "EonformPerlinNode.h"
#include "EonformTerraceNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeRegionalGraph()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Perlin;
		Perlin.Id = FGuid(9701, 1, 1, 1);
		Perlin.Type = EonformTerrainNodeTypes::PerlinNoise;
		Perlin.NumericParameters.Add(TEXT("Scale"), 0.63);
		Perlin.IntegerParameters.Add(TEXT("Octaves"), 6);
		Perlin.NumericParameters.Add(TEXT("Gain"), 0.47);
		Perlin.IntegerParameters.Add(TEXT("Seed"), 7291);
		Perlin.NameParameters.Add(TEXT("WarpType"), TEXT("None"));

		FEonformTerrainNode Terrace;
		Terrace.Id = FGuid(9702, 2, 2, 2);
		Terrace.Type = EonformTerrainNodeTypes::Terrace;
		Terrace.IntegerParameters.Add(TEXT("Terraces"), 11);
		Terrace.NumericParameters.Add(TEXT("Uniformity"), 0.72);
		Terrace.NumericParameters.Add(TEXT("Steepness"), 0.31);
		Terrace.NumericParameters.Add(TEXT("Intensity"), 0.8);
		Terrace.IntegerParameters.Add(TEXT("Seed"), 9182);

		FEonformTerrainNode Constant;
		Constant.Id = FGuid(9703, 3, 3, 3);
		Constant.Type = EonformTerrainNodeTypes::Constant;
		Constant.NameParameters.Add(TEXT("Output"), TEXT("Noise"));
		Constant.NumericParameters.Add(TEXT("Height"), 0.25);

		FEonformTerrainNode Combine;
		Combine.Id = FGuid(9704, 4, 4, 4);
		Combine.Type = EonformTerrainNodeTypes::Combine;
		Combine.NameParameters.Add(TEXT("Mode"), TEXT("Max"));
		Combine.NumericParameters.Add(TEXT("Ratio"), 1.0);

		FEonformTerrainNode Clamp;
		Clamp.Id = FGuid(9705, 5, 5, 5);
		Clamp.Type = EonformTerrainNodeTypes::Clamp;
		Clamp.NameParameters.Add(TEXT("Mode"), TEXT("Standard"));
		Clamp.NumericParameters.Add(TEXT("ValueMin"), 0.2);
		Clamp.NumericParameters.Add(TEXT("ValueMax"), 0.8);

		FEonformTerrainConnection C0;
		C0.FromNode = Perlin.Id;
		C0.FromOutput = TEXT("Out");
		C0.ToNode = Terrace.Id;
		C0.ToInput = TEXT("Terrain");

		FEonformTerrainConnection C1;
		C1.FromNode = Terrace.Id;
		C1.FromOutput = TEXT("Out");
		C1.ToNode = Combine.Id;
		C1.ToInput = TEXT("Input1");

		FEonformTerrainConnection C2;
		C2.FromNode = Constant.Id;
		C2.FromOutput = TEXT("Out");
		C2.ToNode = Combine.Id;
		C2.ToInput = TEXT("Input2");

		FEonformTerrainConnection C3;
		C3.FromNode = Combine.Id;
		C3.FromOutput = TEXT("Out");
		C3.ToNode = Clamp.Id;
		C3.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Perlin, Terrace, Constant, Combine, Clamp };
		Recipe.Connections = { C0, C1, C2, C3 };
		Recipe.OutputNode = Clamp.Id;
		return Recipe;
	}

	FEonformTerrainEvaluationContext FullContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(129, 129);
		Context.ReferenceResolution = FIntPoint(129, 129);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(1280.0, 1280.0, 1200.0, 0.0);
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalGraphEquivalenceTest,
	"Eonform.Core.RegionalEvaluation.GraphMatchesFullWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalGraphEquivalenceTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	RegisterEonformTerraceNode();
	RegisterEonformConstantNode();
	RegisterEonformCombineNode();
	RegisterEonformClampNode();

	const FEonformTerrainRecipe Recipe = MakeRegionalGraph();
	const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Recipe);
	TestTrue(TEXT("Test graph is admitted by regional safety audit"), Support.bSupported);
	if (!Support.bSupported)
	{
		AddError(Support.Describe());
		return false;
	}

	const FEonformTerrainEvaluationContext Full = FullContext();
	const FEonformTerrainEvaluationResult FullResult = FEonformTerrainEvaluator::Evaluate(Recipe, Full);
	TestTrue(TEXT("Full graph evaluates"), FullResult.bSuccess);
	if (!FullResult.bSuccess)
	{
		AddError(FullResult.Error);
		return false;
	}

	const FEonformScalarField* FullHeight = FullResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Full Height exists"), FullHeight);
	if (!FullHeight) return false;

	for (int32 RegionY = 0; RegionY < 2; ++RegionY)
	{
		for (int32 RegionX = 0; RegionX < 2; ++RegionX)
		{
			const int32 StartX = RegionX * 64;
			const int32 StartY = RegionY * 64;
			const int32 EndX = StartX + 64;
			const int32 EndY = StartY + 64;

			FEonformTerrainEvaluationContext Region = Full;
			Region.TargetResolution = FIntPoint(65, 65);
			Region.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(StartX, StartY);
			Region.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(EndX, EndY);
			Region.Region.BorderSamples = 4;

			const FEonformTerrainEvaluationResult RegionResult = FEonformTerrainEvaluator::Evaluate(Recipe, Region);
			TestTrue(TEXT("Regional graph evaluates"), RegionResult.bSuccess);
			if (!RegionResult.bSuccess)
			{
				AddError(RegionResult.Error);
				return false;
			}

			const FEonformScalarField* RegionHeight = RegionResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
			TestNotNull(TEXT("Regional Height exists"), RegionHeight);
			if (!RegionHeight) return false;

			for (int32 Y = 0; Y < 65; ++Y)
			{
				for (int32 X = 0; X < 65; ++X)
				{
					const float Expected = FullHeight->AtInterior(StartX + X, StartY + Y);
					const float Actual = RegionHeight->AtInterior(X, Y);
					if (!FMath::IsNearlyEqual(Expected, Actual, 1.e-5f))
					{
						AddError(FString::Printf(
							TEXT("Region %d,%d differs at local %d,%d: full %.9f regional %.9f"),
							RegionX, RegionY, X, Y, Expected, Actual));
						return false;
					}
				}
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalGraphAdmitsStreamedRidgeTest,
	"Eonform.Core.RegionalEvaluation.RidgeUsesGlobalSummaryContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalGraphAdmitsStreamedRidgeTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;
	FEonformTerrainNode Ridge;
	Ridge.Id = FGuid(9710, 10, 10, 10);
	Ridge.Type = EonformTerrainNodeTypes::Ridge;
	Recipe.Nodes.Add(Ridge);
	Recipe.OutputNode = Ridge.Id;

	const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Recipe);
	TestTrue(TEXT("Ridge is admitted through its streamed global-summary contract"), Support.bSupported);
	TestEqual(TEXT("Streamed Ridge requires no regional halo"), Support.RequiredBorderSamples, 0);
	return Support.bSupported;
}

#endif
