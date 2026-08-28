#if WITH_DEV_AUTOMATION_TESTS

#include "EonformPerlinNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakePerlinRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Perlin;
		Perlin.Id = FGuid(9401, 1, 1, 1);
		Perlin.Type = EonformTerrainNodeTypes::PerlinNoise;
		Perlin.NameParameters.Add(TEXT("Type"), TEXT("FBM"));
		Perlin.NumericParameters.Add(TEXT("Scale"), 0.43);
		Perlin.IntegerParameters.Add(TEXT("Octaves"), 6);
		Perlin.NumericParameters.Add(TEXT("Gain"), 0.55);
		Perlin.IntegerParameters.Add(TEXT("Seed"), 1731);
		Perlin.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
		Perlin.NumericParameters.Add(TEXT("WarpFrequency"), 0.08);
		Perlin.NumericParameters.Add(TEXT("WarpAmplitude"), 0.31);
		Perlin.IntegerParameters.Add(TEXT("WarpOctaves"), 5);
		Recipe.Nodes.Add(Perlin);
		Recipe.OutputNode = Perlin.Id;
		return Recipe;
	}

	FEonformTerrainEvaluationContext FullContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(129, 129);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(12000.0, 8000.0, 2500.0, 0.0);
		return Context;
	}

	FEonformTerrainEvaluationContext UpperRightRegionContext(int32 BorderSamples = 0)
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(65, 65);
		Context.ReferenceResolution = FIntPoint(129, 129);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(12000.0, 8000.0, 2500.0, 0.0);
		Context.Region.WorldMinCm = FVector2d(0.0, 0.0);
		Context.Region.WorldMaxCm = FVector2d(600000.0, 400000.0);
		Context.Region.BorderSamples = BorderSamples;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalPerlinMatchesFullWorldTest,
	"Eonform.Core.Graph.Regional.PerlinMatchesFullWorldWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalPerlinMatchesFullWorldTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	const FEonformTerrainRecipe Recipe = MakePerlinRecipe();
	const FEonformTerrainEvaluationResult Full = FEonformTerrainEvaluator::Evaluate(Recipe, FullContext());
	const FEonformTerrainEvaluationResult Region = FEonformTerrainEvaluator::Evaluate(Recipe, UpperRightRegionContext());

	TestTrue(TEXT("Full Perlin evaluation succeeds"), Full.bSuccess);
	TestTrue(TEXT("Regional Perlin evaluation succeeds"), Region.bSuccess);
	if (!Full.bSuccess)
	{
		AddError(Full.Error);
		return false;
	}
	if (!Region.bSuccess)
	{
		AddError(Region.Error);
		return false;
	}

	const FEonformScalarField* FullHeight = Full.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* RegionHeight = Region.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Full Height exists"), FullHeight);
	TestNotNull(TEXT("Regional Height exists"), RegionHeight);
	if (!FullHeight || !RegionHeight) return false;

	TestEqual(TEXT("Regional interior resolution is preserved"), RegionHeight->Domain.Dimensions, FIntPoint(65, 65));
	TestEqual(TEXT("Regional world minimum is preserved"), RegionHeight->Domain.WorldMin, FVector2d(0.0, 0.0));
	TestEqual(TEXT("Regional world maximum is preserved"), RegionHeight->Domain.WorldMax, FVector2d(600000.0, 400000.0));

	for (int32 Y = 0; Y < 65; ++Y)
	{
		for (int32 X = 0; X < 65; ++X)
		{
			const float Expected = FullHeight->AtInterior(X + 64, Y + 64);
			const float Actual = RegionHeight->AtInterior(X, Y);
			if (!FMath::IsNearlyEqual(Expected, Actual, 1.e-5f))
			{
				AddError(FString::Printf(
					TEXT("Regional Perlin diverged at %d,%d: expected %.9f, got %.9f."),
					X, Y, Expected, Actual));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalPerlinHaloMatchesFullWorldTest,
	"Eonform.Core.Graph.Regional.PerlinHaloUsesGlobalCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalPerlinHaloMatchesFullWorldTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	const FEonformTerrainRecipe Recipe = MakePerlinRecipe();
	const FEonformTerrainEvaluationResult Full = FEonformTerrainEvaluator::Evaluate(Recipe, FullContext());
	const FEonformTerrainEvaluationResult Region = FEonformTerrainEvaluator::Evaluate(Recipe, UpperRightRegionContext(4));
	if (!Full.bSuccess || !Region.bSuccess)
	{
		if (!Full.bSuccess) AddError(Full.Error);
		if (!Region.bSuccess) AddError(Region.Error);
		return false;
	}

	const FEonformScalarField* FullHeight = Full.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* RegionHeight = Region.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (!FullHeight || !RegionHeight) return false;

	TestEqual(TEXT("Regional halo is retained"), RegionHeight->Domain.BorderSamples, 4);
	const float ExpectedTopLeftHalo = FullHeight->AtInterior(60, 60);
	const float ActualTopLeftHalo = RegionHeight->AtStorage(0, 0);
	TestTrue(
		TEXT("Halo sample matches corresponding global sample"),
		FMath::IsNearlyEqual(ExpectedTopLeftHalo, ActualTopLeftHalo, 1.e-5f));
	return true;
}

#endif
