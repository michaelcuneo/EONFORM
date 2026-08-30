#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainLandformNodes.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainEvaluationResult EvaluateMountainVariant(
			FName Style,
			FName Bulk,
			int32 Seed,
			double Scale = 0.98,
			double Height = 0.92,
			int32 TargetResolution = 257)
	{
		FEonformTerrainNode Mountain;
		Mountain.Id = FGuid(0x4D4F554E, 0x5441494E, static_cast<uint32>(Seed), 0x00000001);
		Mountain.Type = EonformTerrainNodeTypes::Mountain;
		Mountain.IntegerParameters.Add(TEXT("Seed"), Seed);
		Mountain.NameParameters.Add(TEXT("Style"), Style);
		Mountain.NameParameters.Add(TEXT("Bulk"), Bulk);
		Mountain.NumericParameters.Add(TEXT("Scale"), Scale);
		Mountain.NumericParameters.Add(TEXT("Height"), Height);

		FEonformTerrainRecipe Recipe;
		Recipe.OutputNode = Mountain.Id;
		Recipe.Nodes.Add(Mountain);

		FEonformTerrainEvaluationContext Context;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(180000.0, 120000.0, 4200.0, 0.0);
		Context.TargetResolution = FIntPoint(TargetResolution, TargetResolution);
		return FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	}

	double MeanAbsoluteDifference(const FEonformScalarField &A, const FEonformScalarField &B)
	{
		if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain)
			return 0.0;
		double Sum = 0.0;
		for (int32 Index = 0; Index < A.Values.Num(); ++Index)
		{
			Sum += FMath::Abs(static_cast<double>(A.Values[Index] - B.Values[Index]));
		}
		return Sum / FMath::Max(1, A.Values.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FEonformTerrainMountainStyleSeparationTest,
		"Eonform.Core.Graph.TerrainMountainStyleSeparation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainMountainStyleSeparationTest::RunTest(const FString &Parameters)
{
	RegisterEonformTerrainLandformNodes();
	const FEonformTerrainEvaluationResult Eroded = EvaluateMountainVariant(TEXT("Eroded"), TEXT("Medium"), 7421, 0.82, 0.9, 97);
	const FEonformTerrainEvaluationResult Alpine = EvaluateMountainVariant(TEXT("Alpine"), TEXT("Medium"), 7421, 0.82, 0.9, 97);
	const FEonformTerrainEvaluationResult Strata = EvaluateMountainVariant(TEXT("Strata"), TEXT("Medium"), 7421, 0.82, 0.9, 97);
	const FEonformTerrainEvaluationResult Badlands = EvaluateMountainVariant(TEXT("Badlands"), TEXT("Medium"), 7421, 0.82, 0.9, 97);
	TestTrue(TEXT("Eroded style evaluates"), Eroded.bSuccess);
	TestTrue(TEXT("Alpine style evaluates"), Alpine.bSuccess);
	TestTrue(TEXT("Strata style evaluates"), Strata.bSuccess);
	TestTrue(TEXT("Badlands style evaluates"), Badlands.bSuccess);
	const FEonformScalarField *ErodedHeight = Eroded.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField *AlpineHeight = Alpine.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField *StrataHeight = Strata.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField *BadlandsHeight = Badlands.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (ErodedHeight && AlpineHeight && StrataHeight && BadlandsHeight)
	{
		TestTrue(TEXT("Alpine materially changes mountain morphology"), MeanAbsoluteDifference(*ErodedHeight, *AlpineHeight) > 0.003);
		TestTrue(TEXT("Strata materially changes mountain morphology"), MeanAbsoluteDifference(*ErodedHeight, *StrataHeight) > 0.003);
		TestTrue(TEXT("Badlands materially changes mountain morphology"), MeanAbsoluteDifference(*ErodedHeight, *BadlandsHeight) > 0.003);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FEonformTerrainCanyonCompositeTest,
		"Eonform.Core.Graph.TerrainCanyonComposite",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainCanyonCompositeTest::RunTest(const FString &Parameters)
{
	RegisterEonformTerrainLandformNodes();
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Canyon descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Canyon, Descriptor));
	TestEqual(TEXT("Canyon is a Terrain node"), Descriptor.Category, FString(TEXT("Terrain")));
	TestEqual(TEXT("Canyon exposes four authored controls"), Descriptor.Parameters.Num(), 4);

	FEonformTerrainNode Canyon;
	Canyon.Id = FGuid(0x43414E59, 0x4F4E3031, 0x00001D7D, 0x00000001);
	Canyon.Type = EonformTerrainNodeTypes::Canyon;
	Canyon.NumericParameters.Add(TEXT("Scale"), 0.72);
	Canyon.NumericParameters.Add(TEXT("Width"), 0.48);
	Canyon.NumericParameters.Add(TEXT("Depth"), 0.85);
	Canyon.IntegerParameters.Add(TEXT("Seed"), 7421);
	FEonformTerrainRecipe Recipe;
	Recipe.OutputNode = Canyon.Id;
	Recipe.Nodes.Add(Canyon);
	FEonformTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(18000.0, 12000.0, 1800.0, 0.0);
	Context.TargetResolution = FIntPoint(97, 97);
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Layered Canyon evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField *Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Canyon publishes Height"), Height);
	TestNotNull(TEXT("Canyon retains hydraulic Wear"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::Wear));
	TestNotNull(TEXT("Canyon retains hydraulic Flow"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::Flow));
	if (!Height)
		return false;

	float Minimum = TNumericLimits<float>::Max();
	float Maximum = TNumericLimits<float>::Lowest();
	for (const float Value : Height->Values)
	{
		Minimum = FMath::Min(Minimum, Value);
		Maximum = FMath::Max(Maximum, Value);
	}
	TestTrue(TEXT("Canyon has deeply incised relief"), Minimum < -0.20f);
	TestTrue(TEXT("Canyon retains mesa and rim relief"), Maximum > 0.12f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FEonformTerrainMountainCompositeTest,
		"Eonform.Core.Graph.TerrainMountainComposite",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainMountainCompositeTest::RunTest(const FString &Parameters)
{
	RegisterEonformTerrainLandformNodes();

	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Mountain descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Mountain, Descriptor));
	TestEqual(TEXT("Mountain is a Terrain node"), Descriptor.Category, FString(TEXT("Terrain")));
	TestEqual(TEXT("Mountain uses Gaea's eight-property contract"), Descriptor.Parameters.Num(), 8);

	const TSet<FName> ExpectedParameters = {
			TEXT("Scale"), TEXT("Height"), TEXT("Style"), TEXT("Bulk"),
			TEXT("ReduceDetails"), TEXT("Seed"), TEXT("X"), TEXT("Y")};
	for (const FName ParameterName : ExpectedParameters)
	{
		TestTrue(
				*FString::Printf(TEXT("Mountain exposes %s"), *ParameterName.ToString()),
				Descriptor.Parameters.ContainsByPredicate([ParameterName](const FEonformTerrainParameterDescriptor &Parameter)
																									{ return Parameter.Name == ParameterName; }));
	}

	// Keep automation inexpensive while proving that requested working resolution
	// reaches the source/composite instead of being applied only as a final mesh resample.
	constexpr int32 TestResolution = 257;
	const FEonformTerrainEvaluationResult Result = EvaluateMountainVariant(TEXT("Alpine"), TEXT("Medium"), 4451, 0.98, 0.92, TestResolution);
	TestTrue(TEXT("Mountain graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField *Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField *Ridge = Result.Dataset.FindScalarField(EonformTerrainFieldNames::RidgeNetwork);
	TestNotNull(TEXT("Mountain publishes Height"), Height);
	TestNotNull(TEXT("Mountain publishes mass"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::MountainMass));
	TestNotNull(TEXT("Mountain publishes uplift"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::Uplift));
	TestNotNull(TEXT("Mountain publishes ridges"), Ridge);
	TestNotNull(TEXT("Mountain publishes drainage readiness"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::DrainageReadiness));
	TestNotNull(TEXT("Mountain publishes erosion eligibility"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::ErosionEligibility));
	TestNotNull(TEXT("Mountain publishes rock exposure"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::RockExposure));
	TestNotNull(TEXT("Mountain publishes cryosphere eligibility"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::CryosphereEligibility));
	TestNotNull(TEXT("Mountain derives slope context"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees));
	TestNotNull(TEXT("Mountain derives concavity context"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::Concavity));

	TestNull(TEXT("Mountain does not derive FlowDirection"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::FlowDirection));
	TestNull(TEXT("Mountain does not derive FlowAccumulation"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::FlowAccumulation));
	TestNull(TEXT("Mountain does not derive CatchmentAreaKm2"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::CatchmentAreaKm2));
	TestNull(TEXT("Mountain does not derive DistanceToOutletKm"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::DistanceToOutletKm));
	TestNull(TEXT("Mountain does not derive StreamOrder"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::StreamOrder));

	if (Height)
	{
		TestEqual(TEXT("Mountain evaluates at requested working width"), Height->Domain.Dimensions.X, TestResolution);
		TestEqual(TEXT("Mountain evaluates at requested working depth"), Height->Domain.Dimensions.Y, TestResolution);

		const FVector2d WorldSizeCm = Height->Domain.WorldSize();
		TestTrue(TEXT("Mountain honors rectangular physical width"), FMath::IsNearlyEqual(FMath::Abs(WorldSizeCm.X), 18000000.0, 1.0));
		TestTrue(TEXT("Mountain honors rectangular physical depth"), FMath::IsNearlyEqual(FMath::Abs(WorldSizeCm.Y), 12000000.0, 1.0));

		float MinHeight = TNumericLimits<float>::Max();
		float MaxHeight = TNumericLimits<float>::Lowest();
		TArray<float> SortedHeights;
		SortedHeights.Reserve(Height->Values.Num());
		for (const float Value : Height->Values)
		{
			MinHeight = FMath::Min(MinHeight, Value);
			MaxHeight = FMath::Max(MaxHeight, Value);
			if (Value > 0.01f)
				SortedHeights.Add(Value);
		}
		TestTrue(TEXT("Mountain has meaningful relief"), MaxHeight - MinHeight > 0.55f);
		TestTrue(TEXT("Mountain retains requested summit scale"), MaxHeight > 0.82f && MaxHeight <= 0.925f);

		int32 MountainSamples = 0;
		int32 NearPeakSamples = 0;
		int32 SummitSamples = 0;
		for (const float Value : Height->Values)
		{
			if (Value > MaxHeight * 0.10f)
				++MountainSamples;
			if (Value > MaxHeight * 0.985f)
				++NearPeakSamples;
			if (Value > MaxHeight * 0.90f)
				++SummitSamples;
		}
		const float PeakFraction = MountainSamples > 0
																	 ? static_cast<float>(NearPeakSamples) / static_cast<float>(MountainSamples)
																	 : 1.0f;
		TestTrue(TEXT("Mountain summit is not a saturated mesa"), PeakFraction < 0.025f);
		TestTrue(TEXT("Mountain summit is a spatial landform, not a one-sample needle"), SummitSamples >= 8);

		if (SortedHeights.Num() > 1000)
		{
			SortedHeights.Sort();
			const int32 P999Index = FMath::Clamp(
					FMath::FloorToInt(static_cast<double>(SortedHeights.Num() - 1) * 0.999),
					0,
					SortedHeights.Num() - 1);
			const float P999 = SortedHeights[P999Index];
			TestTrue(TEXT("Mountain maximum is supported by surrounding summit relief"), MaxHeight - P999 < 0.16f);
		}

		float MaxNeighborDelta = 0.0f;
		const int32 Width = Height->Domain.Dimensions.X;
		const int32 Depth = Height->Domain.Dimensions.Y;
		for (int32 Y = 0; Y < Depth; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float H = Height->AtInterior(X, Y);
				if (X + 1 < Width)
				{
					MaxNeighborDelta = FMath::Max(MaxNeighborDelta, FMath::Abs(H - Height->AtInterior(X + 1, Y)));
				}
				if (Y + 1 < Depth)
				{
					MaxNeighborDelta = FMath::Max(MaxNeighborDelta, FMath::Abs(H - Height->AtInterior(X, Y + 1)));
				}
			}
		}
		TestTrue(TEXT("Mountain has no pathological single-cell vertical blades"), MaxNeighborDelta < 0.22f);
	}

	if (Ridge)
	{
		int32 StrongRidgeSamples = 0;
		for (const float Value : Ridge->Values)
		{
			if (Value > 0.35f)
				++StrongRidgeSamples;
		}
		TestTrue(TEXT("Mountain contains a spatial ridge network"), StrongRidgeSamples > Ridge->Values.Num() / 150);
	}

	return true;
}

#endif
