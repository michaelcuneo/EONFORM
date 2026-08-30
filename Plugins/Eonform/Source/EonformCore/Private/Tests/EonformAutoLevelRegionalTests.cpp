#if WITH_DEV_AUTOMATION_TESTS

#include "EonformAutoLevelNode.h"
#include "EonformBlurNode.h"
#include "EonformPerlinNode.h"
#include "EonformSlopeNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainConnection Connect(const FGuid& From, FName FromOutput, const FGuid& To, FName ToInput)
	{
		FEonformTerrainConnection C;
		C.FromNode = From;
		C.FromOutput = FromOutput;
		C.ToNode = To;
		C.ToInput = ToInput;
		return C;
	}

	FEonformTerrainNode MakePerlin(const FGuid& Id)
	{
		FEonformTerrainNode Node;
		Node.Id = Id;
		Node.Type = EonformTerrainNodeTypes::PerlinNoise;
		Node.NumericParameters.Add(TEXT("Scale"), 0.57);
		Node.IntegerParameters.Add(TEXT("Octaves"), 5);
		Node.NumericParameters.Add(TEXT("Gain"), 0.49);
		Node.IntegerParameters.Add(TEXT("Seed"), 14731);
		Node.NameParameters.Add(TEXT("WarpType"), TEXT("None"));
		return Node;
	}

	FEonformTerrainEvaluationContext FullContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(129, 129);
		Context.ReferenceResolution = FIntPoint(129, 129);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(1280.0, 1280.0, 1200.0, 0.0);
		return Context;
	}

	FEonformTerrainRecipe MakeTerrainRecipe()
	{
		FEonformTerrainRecipe Recipe;
		const FEonformTerrainNode Perlin = MakePerlin(FGuid(0xA1100001, 0xA1100002, 0xA1100003, 0xA1100004));

		FEonformTerrainNode BlurBefore;
		BlurBefore.Id = FGuid(0xA1200001, 0xA1200002, 0xA1200003, 0xA1200004);
		BlurBefore.Type = EonformTerrainNodeTypes::Blur;
		BlurBefore.NumericParameters.Add(TEXT("Radius"), 0.4);

		FEonformTerrainNode AutoLevel;
		AutoLevel.Id = FGuid(0xA1300001, 0xA1300002, 0xA1300003, 0xA1300004);
		AutoLevel.Type = EonformTerrainNodeTypes::AutoLevel;

		FEonformTerrainNode BlurAfter;
		BlurAfter.Id = FGuid(0xA1400001, 0xA1400002, 0xA1400003, 0xA1400004);
		BlurAfter.Type = EonformTerrainNodeTypes::Blur;
		BlurAfter.NumericParameters.Add(TEXT("Radius"), 0.4);

		Recipe.Nodes = { Perlin, BlurBefore, AutoLevel, BlurAfter };
		Recipe.Connections = {
			Connect(Perlin.Id, TEXT("Out"), BlurBefore.Id, TEXT("Input")),
			Connect(BlurBefore.Id, TEXT("Out"), AutoLevel.Id, TEXT("Input")),
			Connect(AutoLevel.Id, TEXT("Out"), BlurAfter.Id, TEXT("Input"))
		};
		Recipe.OutputNode = BlurAfter.Id;
		return Recipe;
	}

	FEonformTerrainRecipe MakeScalarRecipe(FGuid& OutAutoLevelId)
	{
		FEonformTerrainRecipe Recipe;
		const FEonformTerrainNode Perlin = MakePerlin(FGuid(0xA2100001, 0xA2100002, 0xA2100003, 0xA2100004));

		FEonformTerrainNode Slope;
		Slope.Id = FGuid(0xA2200001, 0xA2200002, 0xA2200003, 0xA2200004);
		Slope.Type = EonformTerrainNodeTypes::Slope;
		Slope.NumericParameters.Add(TEXT("RangeMin"), 7.0);
		Slope.NumericParameters.Add(TEXT("RangeMax"), 48.0);
		Slope.NumericParameters.Add(TEXT("Falloff"), 8.0);
		Slope.NumericParameters.Add(TEXT("MicroAccent"), 0.15);
		Slope.NameParameters.Add(TEXT("Type"), TEXT("Accurate"));

		FEonformTerrainNode AutoLevel;
		AutoLevel.Id = FGuid(0xA2300001, 0xA2300002, 0xA2300003, 0xA2300004);
		AutoLevel.Type = EonformTerrainNodeTypes::AutoLevel;
		OutAutoLevelId = AutoLevel.Id;

		Recipe.Nodes = { Perlin, Slope, AutoLevel };
		Recipe.Connections = {
			Connect(Perlin.Id, TEXT("Out"), Slope.Id, TEXT("Terrain")),
			Connect(Slope.Id, TEXT("Mask"), AutoLevel.Id, TEXT("Input"))
		};
		Recipe.OutputNode = AutoLevel.Id;
		return Recipe;
	}

	bool CompareFieldRegion(
		FAutomationTestBase& Test,
		const FEonformScalarField& Full,
		const FEonformScalarField& Region,
		int32 StartX,
		const TCHAR* Label)
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
						TEXT("%s differs at full %d,%d / local %d,%d: full %.9f regional %.9f"),
						Label, StartX + X, Y, X, Y, Expected, Actual));
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalAutoLevelTerrainTest,
	"Eonform.Core.RegionalEvaluation.AutoLevelTerrainMatchesFullWorldAndSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalAutoLevelTerrainTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	RegisterEonformBlurNode();
	RegisterEonformAutoLevelNode();

	const FEonformTerrainRecipe Recipe = MakeTerrainRecipe();
	const FEonformTerrainRegionalSupportReport Support =
		FEonformTerrainRegionalSupport::Analyze(Recipe, FIntPoint(129, 129));
	TestTrue(TEXT("Terrain AutoLevel chain is region-safe"), Support.bSupported);
	if (!Support.bSupported)
	{
		AddError(Support.Describe());
		return false;
	}
	TestEqual(TEXT("AutoLevel adds no halo beyond its two Blur dependencies"), Support.RequiredBorderSamples, 2);

	FEonformTerrainEvaluationContext Full = FullContext();
	const FEonformTerrainEvaluationResult FullResult = FEonformTerrainEvaluator::Evaluate(Recipe, Full);
	TestTrue(TEXT("Full Terrain AutoLevel chain evaluates"), FullResult.bSuccess);
	if (!FullResult.bSuccess)
	{
		AddError(FullResult.Error);
		return false;
	}
	const FEonformScalarField* FullHeight = FullResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Full Terrain AutoLevel Height exists"), FullHeight);
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
	const FEonformTerrainEvaluationResult RightResult = FEonformTerrainEvaluator::Evaluate(Recipe, Right);
	TestTrue(TEXT("Left Terrain AutoLevel region evaluates"), LeftResult.bSuccess);
	TestTrue(TEXT("Right Terrain AutoLevel region evaluates"), RightResult.bSuccess);
	if (!LeftResult.bSuccess || !RightResult.bSuccess)
	{
		if (!LeftResult.bSuccess) AddError(LeftResult.Error);
		if (!RightResult.bSuccess) AddError(RightResult.Error);
		return false;
	}

	const FEonformScalarField* LeftHeight = LeftResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* RightHeight = RightResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (!LeftHeight || !RightHeight) return false;
	if (!CompareFieldRegion(*this, *FullHeight, *LeftHeight, 0, TEXT("Left Terrain AutoLevel"))) return false;
	if (!CompareFieldRegion(*this, *FullHeight, *RightHeight, 64, TEXT("Right Terrain AutoLevel"))) return false;

	for (int32 Y = 0; Y < 129; ++Y)
	{
		if (!FMath::IsNearlyEqual(LeftHeight->AtInterior(64, Y), RightHeight->AtInterior(0, Y), 1.e-5f))
		{
			AddError(FString::Printf(TEXT("Terrain AutoLevel seam differs at row %d"), Y));
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRegionalAutoLevelScalarTest,
	"Eonform.Core.RegionalEvaluation.AutoLevelScalarMatchesFullWorldAndSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRegionalAutoLevelScalarTest::RunTest(const FString& Parameters)
{
	RegisterEonformPerlinNode();
	RegisterEonformSlopeNode();
	RegisterEonformAutoLevelNode();

	FGuid AutoLevelId;
	const FEonformTerrainRecipe Recipe = MakeScalarRecipe(AutoLevelId);
	const FEonformTerrainRegionalSupportReport Support =
		FEonformTerrainRegionalSupport::Analyze(Recipe, FIntPoint(129, 129));
	TestTrue(TEXT("Scalar AutoLevel chain is region-safe"), Support.bSupported);
	if (!Support.bSupported)
	{
		AddError(Support.Describe());
		return false;
	}
	TestEqual(TEXT("Scalar AutoLevel preserves Slope dependency margin"), Support.RequiredBorderSamples, 1);

	FEonformTerrainEvaluationContext Full = FullContext();
	FEonformTerrainValue FullValue;
	FString Error;
	if (!FEonformTerrainEvaluator::EvaluateOutput(Recipe, Full, AutoLevelId, TEXT("Out"), FullValue, &Error))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Full scalar AutoLevel output is scalar"), FullValue.Type == EEonformTerrainValueType::ScalarField);
	if (FullValue.Type != EEonformTerrainValueType::ScalarField) return false;
	const FEonformScalarField& FullField = FullValue.ScalarField;

	FEonformTerrainEvaluationContext Left = Full;
	Left.TargetResolution = FIntPoint(65, 129);
	Left.Region.WorldMinCm = FullField.Domain.InteriorSampleToWorld(0, 0);
	Left.Region.WorldMaxCm = FullField.Domain.InteriorSampleToWorld(64, 128);
	Left.Region.BorderSamples = Support.RequiredBorderSamples;

	FEonformTerrainEvaluationContext Right = Full;
	Right.TargetResolution = FIntPoint(65, 129);
	Right.Region.WorldMinCm = FullField.Domain.InteriorSampleToWorld(64, 0);
	Right.Region.WorldMaxCm = FullField.Domain.InteriorSampleToWorld(128, 128);
	Right.Region.BorderSamples = Support.RequiredBorderSamples;

	FEonformTerrainValue LeftValue;
	FEonformTerrainValue RightValue;
	if (!FEonformTerrainEvaluator::EvaluateOutput(Recipe, Left, AutoLevelId, TEXT("Out"), LeftValue, &Error))
	{
		AddError(Error);
		return false;
	}
	if (!FEonformTerrainEvaluator::EvaluateOutput(Recipe, Right, AutoLevelId, TEXT("Out"), RightValue, &Error))
	{
		AddError(Error);
		return false;
	}
	if (LeftValue.Type != EEonformTerrainValueType::ScalarField || RightValue.Type != EEonformTerrainValueType::ScalarField)
	{
		AddError(TEXT("Regional scalar AutoLevel did not return scalar fields."));
		return false;
	}

	if (!CompareFieldRegion(*this, FullField, LeftValue.ScalarField, 0, TEXT("Left scalar AutoLevel"))) return false;
	if (!CompareFieldRegion(*this, FullField, RightValue.ScalarField, 64, TEXT("Right scalar AutoLevel"))) return false;
	for (int32 Y = 0; Y < 129; ++Y)
	{
		if (!FMath::IsNearlyEqual(
			LeftValue.ScalarField.AtInterior(64, Y),
			RightValue.ScalarField.AtInterior(0, Y),
			1.e-5f))
		{
			AddError(FString::Printf(TEXT("Scalar AutoLevel seam differs at row %d"), Y));
			return false;
		}
	}
	return true;
}

#endif
