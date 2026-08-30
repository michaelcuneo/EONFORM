#if WITH_DEV_AUTOMATION_TESTS

#include "EonformAngleNode.h"
#include "EonformCurvatureNode.h"
#include "EonformDenoiseNode.h"
#include "EonformPerlinNode.h"
#include "EonformSharpenNode.h"
#include "EonformSlopeNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainNode MakePerlin(const FGuid& Id)
	{
		FEonformTerrainNode Node;
		Node.Id = Id;
		Node.Type = EonformTerrainNodeTypes::PerlinNoise;
		Node.NumericParameters.Add(TEXT("Scale"), 0.61);
		Node.IntegerParameters.Add(TEXT("Octaves"), 5);
		Node.NumericParameters.Add(TEXT("Gain"), 0.47);
		Node.IntegerParameters.Add(TEXT("Seed"), 61337);
		Node.NameParameters.Add(TEXT("WarpType"), TEXT("None"));
		return Node;
	}

	FEonformTerrainEvaluationContext FullContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(65, 65);
		Context.ReferenceResolution = FIntPoint(65, 65);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(640.0, 640.0, 1200.0, 0.0);
		return Context;
	}

	FEonformTerrainRecipe MakeDenoiseSharpenRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Perlin = MakePerlin(FGuid(9810, 10, 10, 10));

		FEonformTerrainNode Denoise;
		Denoise.Id = FGuid(9811, 11, 11, 11);
		Denoise.Type = EonformTerrainNodeTypes::Denoise;
		Denoise.NameParameters.Add(TEXT("Type"), TEXT("Two Pass"));
		Denoise.NumericParameters.Add(TEXT("Amount"), 0.55);
		Denoise.IntegerParameters.Add(TEXT("Passes"), 2);

		FEonformTerrainNode Sharpen;
		Sharpen.Id = FGuid(9812, 12, 12, 12);
		Sharpen.Type = EonformTerrainNodeTypes::Sharpen;
		Sharpen.NameParameters.Add(TEXT("Method"), TEXT("Frequency"));
		Sharpen.NumericParameters.Add(TEXT("Amount"), 0.7);

		FEonformTerrainConnection C0;
		C0.FromNode = Perlin.Id;
		C0.FromOutput = TEXT("Out");
		C0.ToNode = Denoise.Id;
		C0.ToInput = TEXT("Input");

		FEonformTerrainConnection C1;
		C1.FromNode = Denoise.Id;
		C1.FromOutput = TEXT("Out");
		C1.ToNode = Sharpen.Id;
		C1.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Perlin, Denoise, Sharpen };
		Recipe.Connections = { C0, C1 };
		Recipe.OutputNode = Sharpen.Id;
		return Recipe;
	}

	FEonformTerrainRecipe MakeDerivativeRecipe(FName Type, const FGuid& Id)
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Perlin = MakePerlin(FGuid(Id.A - 1, Id.B, Id.C, Id.D));
		FEonformTerrainNode Derive;
		Derive.Id = Id;
		Derive.Type = Type;

		FEonformTerrainConnection Connection;
		Connection.FromNode = Perlin.Id;
		Connection.FromOutput = TEXT("Out");
		Connection.ToNode = Derive.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Perlin, Derive };
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Derive.Id;
		return Recipe;
	}

	bool CompareRegion(
		FAutomationTestBase& Test,
		const FEonformScalarField& Full,
		const FEonformScalarField& Region,
		int32 StartX)
	{
		for (int32 Y = 0; Y < Region.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Region.Domain.Dimensions.X; ++X)
			{
				const float Expected = Full.AtInterior(StartX + X, Y);
				const float Actual = Region.AtInterior(X, Y);
				if (!FMath::IsNearlyEqual(Expected, Actual, 1.e-5f))
				{
					Test.AddError(FString::Printf(
						TEXT("Regional neighbourhood chain differs at full %d,%d / local %d,%d: full %.9f regional %.9f"),
						StartX + X, Y, X, Y, Expected, Actual));
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformLocalNeighbourhoodSupportTest,
	"Eonform.Core.RegionalEvaluation.LocalNeighbourhoodSupportContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformLocalNeighbourhoodSupportTest::RunTest(const FString& Parameters)
{
	const FEonformTerrainRegionalSupportReport ChainSupport =
		FEonformTerrainRegionalSupport::Analyze(MakeDenoiseSharpenRecipe(), FIntPoint(65, 65));
	TestTrue(TEXT("Denoise -> Sharpen is region-supported"), ChainSupport.bSupported);
	TestEqual(TEXT("Two-pass Denoise x2 plus Sharpen requires five samples"), ChainSupport.RequiredBorderSamples, 5);

	const FEonformTerrainRegionalSupportReport SlopeSupport =
		FEonformTerrainRegionalSupport::Analyze(MakeDerivativeRecipe(EonformTerrainNodeTypes::Slope, FGuid(9821, 21, 21, 21)), FIntPoint(65, 65));
	const FEonformTerrainRegionalSupportReport CurvatureSupport =
		FEonformTerrainRegionalSupport::Analyze(MakeDerivativeRecipe(EonformTerrainNodeTypes::Curvature, FGuid(9822, 22, 22, 22)), FIntPoint(65, 65));
	const FEonformTerrainRegionalSupportReport AngleSupport =
		FEonformTerrainRegionalSupport::Analyze(MakeDerivativeRecipe(EonformTerrainNodeTypes::Angle, FGuid(9823, 23, 23, 23)), FIntPoint(65, 65));

	TestTrue(TEXT("Slope is region-supported"), SlopeSupport.bSupported);
	TestTrue(TEXT("Curvature is region-supported"), CurvatureSupport.bSupported);
	TestTrue(TEXT("Angle is region-supported"), AngleSupport.bSupported);
	TestEqual(TEXT("Slope requires one sample"), SlopeSupport.RequiredBorderSamples, 1);
	TestEqual(TEXT("Curvature requires one sample"), CurvatureSupport.RequiredBorderSamples, 1);
	TestEqual(TEXT("Angle requires one sample"), AngleSupport.RequiredBorderSamples, 1);
	return ChainSupport.bSupported && SlopeSupport.bSupported && CurvatureSupport.bSupported && AngleSupport.bSupported;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformLocalNeighbourhoodEquivalenceTest,
	"Eonform.Core.RegionalEvaluation.DenoiseSharpenMatchesFullWorldAndSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformLocalNeighbourhoodEquivalenceTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	RegisterEonformDenoiseNode();
	RegisterEonformSharpenNode();

	const FEonformTerrainRecipe Recipe = MakeDenoiseSharpenRecipe();
	const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Recipe, FIntPoint(65, 65));
	TestTrue(TEXT("Neighbourhood chain passes regional audit"), Support.bSupported);
	TestEqual(TEXT("Neighbourhood chain dependency margin"), Support.RequiredBorderSamples, 5);
	if (!Support.bSupported) return false;

	FEonformTerrainEvaluationContext Full = FullContext();
	const FEonformTerrainEvaluationResult FullResult = FEonformTerrainEvaluator::Evaluate(Recipe, Full);
	TestTrue(TEXT("Full neighbourhood chain evaluates"), FullResult.bSuccess);
	if (!FullResult.bSuccess)
	{
		AddError(FullResult.Error);
		return false;
	}

	const FEonformScalarField* FullHeight = FullResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Full neighbourhood Height exists"), FullHeight);
	if (!FullHeight) return false;

	FEonformTerrainEvaluationContext Left = Full;
	Left.TargetResolution = FIntPoint(33, 65);
	Left.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(0, 0);
	Left.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(32, 64);
	Left.Region.BorderSamples = Support.RequiredBorderSamples;

	FEonformTerrainEvaluationContext Right = Full;
	Right.TargetResolution = FIntPoint(33, 65);
	Right.Region.WorldMinCm = FullHeight->Domain.InteriorSampleToWorld(32, 0);
	Right.Region.WorldMaxCm = FullHeight->Domain.InteriorSampleToWorld(64, 64);
	Right.Region.BorderSamples = Support.RequiredBorderSamples;

	const FEonformTerrainEvaluationResult LeftResult = FEonformTerrainEvaluator::Evaluate(Recipe, Left);
	const FEonformTerrainEvaluationResult RightResult = FEonformTerrainEvaluator::Evaluate(Recipe, Right);
	TestTrue(TEXT("Left neighbourhood region evaluates"), LeftResult.bSuccess);
	TestTrue(TEXT("Right neighbourhood region evaluates"), RightResult.bSuccess);
	if (!LeftResult.bSuccess || !RightResult.bSuccess)
	{
		if (!LeftResult.bSuccess) AddError(LeftResult.Error);
		if (!RightResult.bSuccess) AddError(RightResult.Error);
		return false;
	}

	const FEonformScalarField* LeftHeight = LeftResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* RightHeight = RightResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Left neighbourhood Height exists"), LeftHeight);
	TestNotNull(TEXT("Right neighbourhood Height exists"), RightHeight);
	if (!LeftHeight || !RightHeight) return false;

	if (!CompareRegion(*this, *FullHeight, *LeftHeight, 0)) return false;
	if (!CompareRegion(*this, *FullHeight, *RightHeight, 32)) return false;

	for (int32 Y = 0; Y < 65; ++Y)
	{
		if (!FMath::IsNearlyEqual(LeftHeight->AtInterior(32, Y), RightHeight->AtInterior(0, Y), 1.e-5f))
		{
			AddError(FString::Printf(TEXT("Neighbourhood seam differs at row %d."), Y));
			return false;
		}
	}
	return true;
}

#endif
