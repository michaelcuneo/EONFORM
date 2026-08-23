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
		int32 Seed,
		double Scale = 0.98,
		double Height = 0.92)
	{
		FGaeaTerrainNode Mountain;
		Mountain.Id = FGuid(0x4D4F554E, 0x5441494E, static_cast<uint32>(Seed), 0x00000001);
		Mountain.Type = GaeaTerrainNodeTypes::Mountain;
		Mountain.IntegerParameters.Add(TEXT("Seed"), Seed);
		Mountain.NameParameters.Add(TEXT("Style"), Style);
		Mountain.NameParameters.Add(TEXT("Bulk"), Bulk);
		Mountain.NumericParameters.Add(TEXT("Scale"), Scale);
		Mountain.NumericParameters.Add(TEXT("Height"), Height);

		FGaeaTerrainRecipe Recipe;
		Recipe.OutputNode = Mountain.Id;
		Recipe.Nodes.Add(Mountain);

		FGaeaTerrainEvaluationContext Context;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(180000.0, 120000.0, 4200.0, 0.0);
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
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
	TestEqual(TEXT("Mountain uses Gaea's eight-property contract"), Descriptor.Parameters.Num(), 8);

	const TSet<FName> ExpectedParameters = {
		TEXT("Scale"), TEXT("Height"), TEXT("Style"), TEXT("Bulk"),
		TEXT("ReduceDetails"), TEXT("Seed"), TEXT("X"), TEXT("Y")
	};
	for (const FName ParameterName : ExpectedParameters)
	{
		TestTrue(
			*FString::Printf(TEXT("Mountain exposes %s"), *ParameterName.ToString()),
			Descriptor.Parameters.ContainsByPredicate([ParameterName](const FGaeaTerrainParameterDescriptor& Parameter)
			{
				return Parameter.Name == ParameterName;
			}));
	}

	const FGaeaTerrainEvaluationResult Result = EvaluateMountainVariant(TEXT("Alpine"), TEXT("Medium"), 4451);
	TestTrue(TEXT("Mountain graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* Ridge = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::RidgeNetwork);
	TestNotNull(TEXT("Mountain publishes Height"), Height);
	TestNotNull(TEXT("Mountain publishes mass"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::MountainMass));
	TestNotNull(TEXT("Mountain publishes uplift"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Uplift));
	TestNotNull(TEXT("Mountain publishes ridges"), Ridge);
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
		const FVector2d WorldSizeCm = Height->Domain.WorldSize();
		TestTrue(TEXT("Mountain honors rectangular physical width"), FMath::IsNearlyEqual(FMath::Abs(WorldSizeCm.X), 18000000.0, 1.0));
		TestTrue(TEXT("Mountain honors rectangular physical depth"), FMath::IsNearlyEqual(FMath::Abs(WorldSizeCm.Y), 12000000.0, 1.0));

		float MinHeight = TNumericLimits<float>::Max();
		float MaxHeight = TNumericLimits<float>::Lowest();
		for (const float Value : Height->Values)
		{
			MinHeight = FMath::Min(MinHeight, Value);
			MaxHeight = FMath::Max(MaxHeight, Value);
		}
		TestTrue(TEXT("Mountain has meaningful relief"), MaxHeight - MinHeight > 0.55f);
		TestTrue(TEXT("Mountain reaches requested summit height"), FMath::IsNearlyEqual(MaxHeight, 0.92f, 0.025f));

		int32 MountainSamples = 0;
		int32 NearPeakSamples = 0;
		for (const float Value : Height->Values)
		{
			if (Value > MaxHeight * 0.10f) ++MountainSamples;
			if (Value > MaxHeight * 0.985f) ++NearPeakSamples;
		}
		const float PeakFraction = MountainSamples > 0
			? static_cast<float>(NearPeakSamples) / static_cast<float>(MountainSamples)
			: 1.0f;
		TestTrue(TEXT("Mountain summit is a crest/peak, not a saturated mesa"), PeakFraction < 0.025f);
	}

	if (Ridge)
	{
		int32 StrongRidgeSamples = 0;
		for (const float Value : Ridge->Values)
		{
			if (Value > 0.35f) ++StrongRidgeSamples;
		}
		TestTrue(TEXT("Mountain contains a spatial ridge network"), StrongRidgeSamples > Ridge->Values.Num() / 150);
	}

	return true;
}

#endif
