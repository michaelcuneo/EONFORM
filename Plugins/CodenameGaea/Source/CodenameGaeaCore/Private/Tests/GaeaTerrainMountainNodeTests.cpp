#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformNodes.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

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
	TestEqual(TEXT("Mountain parameter contract"), Descriptor.Parameters.Num(), 13);

	FGaeaTerrainNode Mountain;
	Mountain.Id = FGuid(0x4D4F554E, 0x5441494E, 0x00000001, 0x00000001);
	Mountain.Type = GaeaTerrainNodeTypes::Mountain;
	Mountain.IntegerParameters.Add(TEXT("Resolution"), 129);
	Mountain.IntegerParameters.Add(TEXT("Seed"), 4451);
	Mountain.NumericParameters.Add(TEXT("Radius"), 0.72);
	Mountain.NumericParameters.Add(TEXT("Elongation"), 0.48);
	Mountain.NumericParameters.Add(TEXT("RidgeStrength"), 0.78);

	FGaeaTerrainRecipe Recipe;
	Recipe.OutputNode = Mountain.Id;
	Recipe.Nodes.Add(Mountain);

	FGaeaTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(180000.0, 120000.0, 4200.0, 0.0);
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Mountain graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Mountain publishes Height"), Height);
	TestNotNull(TEXT("Mountain publishes mass"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::MountainMass));
	TestNotNull(TEXT("Mountain publishes uplift"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Uplift));
	TestNotNull(TEXT("Mountain publishes ridges"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::RidgeNetwork));
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
		TestEqual(TEXT("Mountain physical resolution X"), Height->Domain.Dimensions.X, 129);
		TestEqual(TEXT("Mountain physical resolution Y"), Height->Domain.Dimensions.Y, 129);
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
		TestTrue(TEXT("Mountain has meaningful relief"), MaxHeight - MinHeight > 0.25f);
	}

	return true;
}

#endif
