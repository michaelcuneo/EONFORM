#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaRidgeNode.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace GaeaRidgeNodeTests
{
	FGaeaTerrainEvaluationResult EvaluateRidge(float Definition, int32 Seed, FIntPoint Resolution)
	{
		RegisterGaeaRidgeNode();

		FGaeaTerrainNode Ridge;
		Ridge.Id = FGuid(0x51D6E001, 0x51D6E002, 0x51D6E003, 0x51D6E004);
		Ridge.Type = GaeaTerrainNodeTypes::Ridge;
		Ridge.NumericParameters.Add(TEXT("Scale"), 0.75);
		Ridge.NumericParameters.Add(TEXT("Height"), 0.6);
		Ridge.NumericParameters.Add(TEXT("Definition"), Definition);
		Ridge.NumericParameters.Add(TEXT("ScaleX"), 1.0);
		Ridge.NumericParameters.Add(TEXT("ScaleY"), 1.0);
		Ridge.IntegerParameters.Add(TEXT("Seed"), Seed);

		FGaeaTerrainRecipe Recipe;
		Recipe.Nodes.Add(Ridge);
		Recipe.OutputNode = Ridge.Id;

		FGaeaTerrainEvaluationContext Context;
		Context.TargetResolution = Resolution;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(12000.0, 8000.0, 2600.0, 0.0);
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	}

	double MeanAbsoluteDifference(const FGaeaScalarField& A, const FGaeaScalarField& B)
	{
		if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain) return 0.0;
		double Sum = 0.0;
		for (int32 Y = 0; Y < A.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < A.Domain.Dimensions.X; ++X)
			{
				Sum += FMath::Abs(static_cast<double>(A.AtInterior(X, Y) - B.AtInterior(X, Y)));
			}
		}
		return Sum / FMath::Max(1, A.Domain.GetInteriorSampleCount());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaRidgeContractTest,
	"CodenameGaea.Core.Graph.Ridge.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaRidgeContractTest::RunTest(const FString& Parameters)
{
	RegisterGaeaRidgeNode();
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Ridge descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Ridge, Descriptor));
	TestEqual(TEXT("Ridge display name"), Descriptor.DisplayName, FString(TEXT("Ridge")));
	TestEqual(TEXT("Ridge category"), Descriptor.Category, FString(TEXT("Terrain")));
	TestEqual(TEXT("Ridge exposes exactly six authored controls"), Descriptor.Parameters.Num(), 6);

	const TArray<FName> Expected = {
		TEXT("Scale"), TEXT("Height"), TEXT("Definition"), TEXT("Seed"), TEXT("ScaleX"), TEXT("ScaleY")
	};
	for (const FName Name : Expected)
	{
		TestTrue(*FString::Printf(TEXT("Ridge exposes %s"), *Name.ToString()), Descriptor.Parameters.ContainsByPredicate(
			[Name](const FGaeaTerrainParameterDescriptor& P) { return P.Name == Name; }));
	}
	TestFalse(TEXT("Resolution is build context, not a Ridge UI parameter"), Descriptor.Parameters.ContainsByPredicate(
		[](const FGaeaTerrainParameterDescriptor& P) { return P.Name == TEXT("Resolution"); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaRidgeResolutionTest,
	"CodenameGaea.Core.Graph.Ridge.Resolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaRidgeResolutionTest::RunTest(const FString& Parameters)
{
	using namespace GaeaRidgeNodeTests;
	const FIntPoint Requested(73, 61);
	const FGaeaTerrainEvaluationResult Result = EvaluateRidge(0.4f, 4451, Requested);
	TestTrue(TEXT("Ridge evaluates"), Result.bSuccess);
	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Ridge publishes Height"), Height);
	if (Height)
	{
		TestEqual(TEXT("Ridge honors target width"), Height->Domain.Dimensions.X, Requested.X);
		TestEqual(TEXT("Ridge honors target height"), Height->Domain.Dimensions.Y, Requested.Y);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaRidgeDefinitionTest,
	"CodenameGaea.Core.Graph.Ridge.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaRidgeDefinitionTest::RunTest(const FString& Parameters)
{
	using namespace GaeaRidgeNodeTests;
	const FGaeaTerrainEvaluationResult Soft = EvaluateRidge(0.15f, 4451, FIntPoint(97, 97));
	const FGaeaTerrainEvaluationResult Defined = EvaluateRidge(0.85f, 4451, FIntPoint(97, 97));
	TestTrue(TEXT("Low-definition Ridge evaluates"), Soft.bSuccess);
	TestTrue(TEXT("High-definition Ridge evaluates"), Defined.bSuccess);
	const FGaeaScalarField* A = Soft.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* B = Defined.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (A && B)
	{
		TestTrue(TEXT("Definition materially changes Ridge morphology"), MeanAbsoluteDifference(*A, *B) > 0.001);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaRidgeDeterminismTest,
	"CodenameGaea.Core.Graph.Ridge.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaRidgeDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace GaeaRidgeNodeTests;
	const FGaeaTerrainEvaluationResult AResult = EvaluateRidge(0.4f, 99217, FIntPoint(65, 65));
	const FGaeaTerrainEvaluationResult BResult = EvaluateRidge(0.4f, 99217, FIntPoint(65, 65));
	TestTrue(TEXT("First deterministic Ridge evaluates"), AResult.bSuccess);
	TestTrue(TEXT("Second deterministic Ridge evaluates"), BResult.bSuccess);
	const FGaeaScalarField* A = AResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* B = BResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (A && B)
	{
		TestTrue(TEXT("Same Ridge seed/settings are deterministic"), MeanAbsoluteDifference(*A, *B) <= UE_SMALL_NUMBER);
	}
	return true;
}

#endif
