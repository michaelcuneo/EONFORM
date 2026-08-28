#if WITH_DEV_AUTOMATION_TESTS

#include "EonformRidgeNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace EonformRidgeNodeTests
{
	FEonformTerrainEvaluationResult EvaluateRidge(float Definition, int32 Seed, FIntPoint Resolution, float Scale = 0.75f)
	{
		RegisterEonformRidgeNode();

		FEonformTerrainNode Ridge;
		Ridge.Id = FGuid(0x51D6E001, 0x51D6E002, 0x51D6E003, 0x51D6E004);
		Ridge.Type = EonformTerrainNodeTypes::Ridge;
		Ridge.NumericParameters.Add(TEXT("Scale"), Scale);
		Ridge.NumericParameters.Add(TEXT("Height"), 0.6);
		Ridge.NumericParameters.Add(TEXT("Definition"), Definition);
		Ridge.NumericParameters.Add(TEXT("ScaleX"), 1.0);
		Ridge.NumericParameters.Add(TEXT("ScaleY"), 1.0);
		Ridge.IntegerParameters.Add(TEXT("Seed"), Seed);

		FEonformTerrainRecipe Recipe;
		Recipe.Nodes.Add(Ridge);
		Recipe.OutputNode = Ridge.Id;

		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = Resolution;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(12000.0, 8000.0, 2600.0, 0.0);
		return FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	}

	double MeanAbsoluteDifference(const FEonformScalarField& A, const FEonformScalarField& B)
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

	float FieldRange(const FEonformScalarField& Field)
	{
		float MinValue = TNumericLimits<float>::Max();
		float MaxValue = TNumericLimits<float>::Lowest();
		for (const float Value : Field.Values)
		{
			MinValue = FMath::Min(MinValue, Value);
			MaxValue = FMath::Max(MaxValue, Value);
		}
		return MaxValue - MinValue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRidgeContractTest,
	"Eonform.Core.Graph.Ridge.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRidgeContractTest::RunTest(const FString& Parameters)
{
	RegisterEonformRidgeNode();
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Ridge descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Ridge, Descriptor));
	TestEqual(TEXT("Ridge display name"), Descriptor.DisplayName, FString(TEXT("Ridge")));
	TestEqual(TEXT("Ridge category"), Descriptor.Category, FString(TEXT("Terrain")));
	TestEqual(TEXT("Ridge exposes exactly six authored controls"), Descriptor.Parameters.Num(), 6);

	const TArray<FName> Expected = {
		TEXT("Scale"), TEXT("Height"), TEXT("Definition"), TEXT("Seed"), TEXT("ScaleX"), TEXT("ScaleY")
	};
	for (const FName Name : Expected)
	{
		TestTrue(*FString::Printf(TEXT("Ridge exposes %s"), *Name.ToString()), Descriptor.Parameters.ContainsByPredicate(
			[Name](const FEonformTerrainParameterDescriptor& P) { return P.Name == Name; }));
	}
	TestFalse(TEXT("Resolution is build context, not a Ridge UI parameter"), Descriptor.Parameters.ContainsByPredicate(
		[](const FEonformTerrainParameterDescriptor& P) { return P.Name == TEXT("Resolution"); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRidgeResolutionTest,
	"Eonform.Core.Graph.Ridge.Resolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRidgeResolutionTest::RunTest(const FString& Parameters)
{
	using namespace EonformRidgeNodeTests;
	const FIntPoint Requested(73, 61);
	const FEonformTerrainEvaluationResult Result = EvaluateRidge(0.4f, 4451, Requested);
	TestTrue(TEXT("Ridge evaluates"), Result.bSuccess);
	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Ridge publishes Height"), Height);
	if (Height)
	{
		TestEqual(TEXT("Ridge honors target width"), Height->Domain.Dimensions.X, Requested.X);
		TestEqual(TEXT("Ridge honors target height"), Height->Domain.Dimensions.Y, Requested.Y);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRidgeDefinitionTest,
	"Eonform.Core.Graph.Ridge.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRidgeDefinitionTest::RunTest(const FString& Parameters)
{
	using namespace EonformRidgeNodeTests;
	const FEonformTerrainEvaluationResult Soft = EvaluateRidge(0.15f, 4451, FIntPoint(97, 97));
	const FEonformTerrainEvaluationResult Defined = EvaluateRidge(0.85f, 4451, FIntPoint(97, 97));
	TestTrue(TEXT("Low-definition Ridge evaluates"), Soft.bSuccess);
	TestTrue(TEXT("High-definition Ridge evaluates"), Defined.bSuccess);
	const FEonformScalarField* A = Soft.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* B = Defined.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (A && B)
	{
		TestTrue(TEXT("Definition materially changes Ridge morphology"), MeanAbsoluteDifference(*A, *B) > 0.001);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRidgeDeterminismTest,
	"Eonform.Core.Graph.Ridge.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRidgeDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace EonformRidgeNodeTests;
	const FEonformTerrainEvaluationResult AResult = EvaluateRidge(0.4f, 99217, FIntPoint(65, 65));
	const FEonformTerrainEvaluationResult BResult = EvaluateRidge(0.4f, 99217, FIntPoint(65, 65));
	TestTrue(TEXT("First deterministic Ridge evaluates"), AResult.bSuccess);
	TestTrue(TEXT("Second deterministic Ridge evaluates"), BResult.bSuccess);
	const FEonformScalarField* A = AResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* B = BResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (A && B)
	{
		TestTrue(TEXT("Same Ridge seed/settings are deterministic"), MeanAbsoluteDifference(*A, *B) <= UE_SMALL_NUMBER);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRidgeRangeRetentionTest,
	"Eonform.Core.Graph.Ridge.RangeRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRidgeRangeRetentionTest::RunTest(const FString& Parameters)
{
	using namespace EonformRidgeNodeTests;
	const FEonformTerrainEvaluationResult DefaultResult = EvaluateRidge(0.4f, 4451, FIntPoint(97, 97), 0.75f);
	const FEonformTerrainEvaluationResult LowScaleResult = EvaluateRidge(0.4f, 4451, FIntPoint(97, 97), 0.35f);
	TestTrue(TEXT("Default Ridge evaluates"), DefaultResult.bSuccess);
	TestTrue(TEXT("Low-scale Ridge evaluates"), LowScaleResult.bSuccess);

	const FEonformScalarField* DefaultHeight = DefaultResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* LowScaleHeight = LowScaleResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Default Ridge publishes Height"), DefaultHeight);
	TestNotNull(TEXT("Low-scale Ridge publishes Height"), LowScaleHeight);
	if (DefaultHeight)
	{
		TestTrue(TEXT("Default Ridge retains vertical variation"), FieldRange(*DefaultHeight) > 0.05f);
	}
	if (LowScaleHeight)
	{
		TestTrue(TEXT("Low-scale Ridge does not collapse to a plane"), FieldRange(*LowScaleHeight) > 0.01f);
	}
	return true;
}

#endif
