#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformNodes.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaTerrainEvaluationResult EvaluateMountainVariant(
		FName Style,
		FName Bulk,
		bool bReduceDetails,
		int32 Seed = 4451)
	{
		FGaeaTerrainNode Mountain;
		Mountain.Id = FGuid(0x4D4F554E, 0x5441494E, static_cast<uint32>(Seed), 0x00000001);
		Mountain.Type = GaeaTerrainNodeTypes::Mountain;
		Mountain.NumericParameters.Add(TEXT("Scale"), 0.88);
		Mountain.NumericParameters.Add(TEXT("Height"), 0.94);
		Mountain.NameParameters.Add(TEXT("Style"), Style);
		Mountain.NameParameters.Add(TEXT("Bulk"), Bulk);
		Mountain.BoolParameters.Add(TEXT("ReduceDetails"), bReduceDetails);
		Mountain.IntegerParameters.Add(TEXT("Seed"), Seed);
		Mountain.NumericParameters.Add(TEXT("X"), 0.0);
		Mountain.NumericParameters.Add(TEXT("Y"), 0.0);

		FGaeaTerrainRecipe Recipe;
		Recipe.OutputNode = Mountain.Id;
		Recipe.Nodes.Add(Mountain);

		FGaeaTerrainEvaluationContext Context;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(180000.0, 120000.0, 4200.0, 0.0);
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	}

	void HeightStats(const FGaeaScalarField& Height, float& OutMin, float& OutMax, double& OutMean, double& OutCoverage)
	{
		OutMin = TNumericLimits<float>::Max();
		OutMax = TNumericLimits<float>::Lowest();
		double Sum = 0.0;
		int64 Covered = 0;
		for (const float Value : Height.Values)
		{
			OutMin = FMath::Min(OutMin, Value);
			OutMax = FMath::Max(OutMax, Value);
			Sum += Value;
			if (Value > 0.05f) ++Covered;
		}
		OutMean = Height.Values.IsEmpty() ? 0.0 : Sum / static_cast<double>(Height.Values.Num());
		OutCoverage = Height.Values.IsEmpty() ? 0.0 : static_cast<double>(Covered) / static_cast<double>(Height.Values.Num());
	}

	double MeanAbsoluteDifference(const FGaeaScalarField& A, const FGaeaScalarField& B)
	{
		if (A.Values.Num() != B.Values.Num() || A.Values.IsEmpty()) return 0.0;
		double Sum = 0.0;
		for (int32 Index = 0; Index < A.Values.Num(); ++Index)
		{
			Sum += FMath::Abs(static_cast<double>(A.Values[Index] - B.Values[Index]));
		}
		return Sum / static_cast<double>(A.Values.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainMountainCompositeTest,
	"CodenameGaea.Core.Graph.TerrainMountainComposite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainMountainCompositeTest::RunTest(const FString& Parameters)
{
	RegisterGaeaTerrainLandformNodes();

	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Mountain descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Mountain, Descriptor));
	TestEqual(TEXT("Mountain is a Terrain node"), Descriptor.Category, FString(TEXT("Terrain")));
	TestEqual(TEXT("Mountain matches the documented eight-property contract"), Descriptor.Parameters.Num(), 8);

	const TArray<FName> ExpectedParameters =
	{
		TEXT("Scale"), TEXT("Height"), TEXT("Style"), TEXT("Bulk"),
		TEXT("ReduceDetails"), TEXT("Seed"), TEXT("X"), TEXT("Y")
	};
	for (const FName Expected : ExpectedParameters)
	{
		TestTrue(*FString::Printf(TEXT("Mountain exposes %s"), *Expected.ToString()), Descriptor.Parameters.ContainsByPredicate(
			[Expected](const FGaeaTerrainParameterDescriptor& Parameter) { return Parameter.Name == Expected; }));
	}

	const FGaeaTerrainEvaluationResult Result = EvaluateMountainVariant(TEXT("Basic"), TEXT("Medium"), false);
	TestTrue(TEXT("Mountain graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* Ridges = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::RidgeNetwork);
	TestNotNull(TEXT("Mountain publishes Height"), Height);
	TestNotNull(TEXT("Mountain publishes mass"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::MountainMass));
	TestNotNull(TEXT("Mountain publishes uplift"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Uplift));
	TestNotNull(TEXT("Mountain publishes ridges"), Ridges);
	TestNotNull(TEXT("Mountain publishes drainage readiness"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::DrainageReadiness));
	TestNotNull(TEXT("Mountain publishes erosion eligibility"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::ErosionEligibility));
	TestNotNull(TEXT("Mountain publishes rock exposure"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::RockExposure));
	TestNotNull(TEXT("Mountain publishes cryosphere eligibility"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::CryosphereEligibility));
	TestNotNull(TEXT("Mountain derives slope context"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees));
	TestNotNull(TEXT("Mountain derives concavity context"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity));

	TestNull(TEXT("Mountain does not derive FlowDirection"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection));
	TestNull(TEXT("Mountain does not derive FlowAccumulation"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation));
	TestNull(TEXT("Mountain does not derive CatchmentAreaKm2"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2));
	TestNull(TEXT("Mountain does not derive DistanceToOutletKm"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::DistanceToOutletKm));
	TestNull(TEXT("Mountain does not derive StreamOrder"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::StreamOrder));

	if (Height)
	{
		TestEqual(TEXT("Mountain physical resolution X"), Height->Domain.Dimensions.X, 513);
		TestEqual(TEXT("Mountain physical resolution Y"), Height->Domain.Dimensions.Y, 513);
		const FVector2d WorldSizeCm = Height->Domain.WorldSize();
		TestTrue(TEXT("Mountain honors rectangular physical width"), FMath::IsNearlyEqual(FMath::Abs(WorldSizeCm.X), 18000000.0, 1.0));
		TestTrue(TEXT("Mountain honors rectangular physical depth"), FMath::IsNearlyEqual(FMath::Abs(WorldSizeCm.Y), 12000000.0, 1.0));

		float MinHeight = 0.0f;
		float MaxHeight = 0.0f;
		double MeanHeight = 0.0;
		double Coverage = 0.0;
		HeightStats(*Height, MinHeight, MaxHeight, MeanHeight, Coverage);
		TestTrue(TEXT("Mountain has meaningful relief"), MaxHeight - MinHeight > 0.45f);
		TestTrue(TEXT("Mountain occupies a meaningful but bounded area"), Coverage > 0.04 && Coverage < 0.75);
		TestTrue(TEXT("Mountain has nontrivial average elevation"), MeanHeight > 0.015);
	}

	if (Ridges)
	{
		float RidgeMax = 0.0f;
		double RidgeSum = 0.0;
		for (const float Value : Ridges->Values)
		{
			RidgeMax = FMath::Max(RidgeMax, Value);
			RidgeSum += Value;
		}
		const double RidgeMean = Ridges->Values.IsEmpty() ? 0.0 : RidgeSum / static_cast<double>(Ridges->Values.Num());
		TestTrue(TEXT("Mountain has strong structural ridges"), RidgeMax > 0.65f);
		TestTrue(TEXT("Mountain ridge network is spatial rather than empty"), RidgeMean > 0.01);
	}

	const FGaeaTerrainEvaluationResult Alpine = EvaluateMountainVariant(TEXT("Alpine"), TEXT("Medium"), false, 7711);
	const FGaeaTerrainEvaluationResult Old = EvaluateMountainVariant(TEXT("Old"), TEXT("Medium"), false, 7711);
	TestTrue(TEXT("Alpine variant evaluates"), Alpine.bSuccess);
	TestTrue(TEXT("Old variant evaluates"), Old.bSuccess);
	if (Alpine.bSuccess && Old.bSuccess)
	{
		const FGaeaScalarField* AlpineHeight = Alpine.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* OldHeight = Old.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (AlpineHeight && OldHeight)
		{
			TestTrue(TEXT("Style changes mountain morphology"), MeanAbsoluteDifference(*AlpineHeight, *OldHeight) > 0.01);
		}
	}

	const FGaeaTerrainEvaluationResult LowBulk = EvaluateMountainVariant(TEXT("Basic"), TEXT("Low"), false, 9081);
	const FGaeaTerrainEvaluationResult HighBulk = EvaluateMountainVariant(TEXT("Basic"), TEXT("High"), false, 9081);
	TestTrue(TEXT("Low bulk evaluates"), LowBulk.bSuccess);
	TestTrue(TEXT("High bulk evaluates"), HighBulk.bSuccess);
	if (LowBulk.bSuccess && HighBulk.bSuccess)
	{
		const FGaeaScalarField* LowHeight = LowBulk.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* HighHeight = HighBulk.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (LowHeight && HighHeight)
		{
			float Min = 0.0f, Max = 0.0f;
			double Mean = 0.0, LowCoverage = 0.0, HighCoverage = 0.0;
			HeightStats(*LowHeight, Min, Max, Mean, LowCoverage);
			HeightStats(*HighHeight, Min, Max, Mean, HighCoverage);
			TestTrue(TEXT("High bulk increases mountain coverage"), HighCoverage > LowCoverage);
		}
	}

	return true;
}

#endif
