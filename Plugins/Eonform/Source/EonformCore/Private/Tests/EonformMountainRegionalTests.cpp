#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainLandformNodes.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeRegionalMountainRecipe(FName Bulk = TEXT("Medium"))
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Mountain;
		Mountain.Id = FGuid(9720, 20, 20, 20);
		Mountain.Type = TEXT("Mountain");
		Mountain.NumericParameters.Add(TEXT("Scale"), 0.56);
		Mountain.NumericParameters.Add(TEXT("Height"), 1.18);
		Mountain.NameParameters.Add(TEXT("Style"), TEXT("Basic"));
		Mountain.NameParameters.Add(TEXT("Bulk"), Bulk);
		Mountain.IntegerParameters.Add(TEXT("Seed"), 41273);
		Mountain.NumericParameters.Add(TEXT("X"), 0.47);
		Mountain.NumericParameters.Add(TEXT("Y"), 0.54);
		Recipe.Nodes.Add(Mountain);
		Recipe.OutputNode = Mountain.Id;
		return Recipe;
	}

	FEonformTerrainRecipe MakeInputMultipliedMountainRecipe()
	{
		FEonformTerrainRecipe Recipe = MakeRegionalMountainRecipe();
		FEonformTerrainNode Source;
		Source.Id = FGuid(9721, 21, 21, 21);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 65);
		Recipe.Nodes.Insert(Source, 0);

		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Recipe.OutputNode;
		Connection.ToInput = TEXT("In");
		Recipe.Connections.Add(Connection);
		return Recipe;
	}

	FEonformTerrainEvaluationContext MountainFullContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(65, 65);
		Context.ReferenceResolution = FIntPoint(65, 65);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(640.0, 640.0, 1200.0, 0.0);
		return Context;
	}

	bool CompareRegionFieldToFull(
		FAutomationTestBase& Test,
		FName FieldName,
		const FEonformTerrainDataset& FullDataset,
		const FEonformTerrainDataset& RegionDataset,
		int32 StartX)
	{
		const FEonformScalarField* Full = FullDataset.FindScalarField(FieldName);
		const FEonformScalarField* Region = RegionDataset.FindScalarField(FieldName);
		if (!Full || !Region)
		{
			Test.AddError(FString::Printf(TEXT("Missing Mountain comparison field %s"), *FieldName.ToString()));
			return false;
		}

		for (int32 Y = 0; Y < Region->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Region->Domain.Dimensions.X; ++X)
			{
				const float Expected = Full->AtInterior(StartX + X, Y);
				const float Actual = Region->AtInterior(X, Y);
				if (!FMath::IsNearlyEqual(Expected, Actual, 1.e-5f))
				{
					Test.AddError(FString::Printf(
						TEXT("Regional Mountain field %s differs at full %d,%d / local %d,%d: full %.9f regional %.9f"),
						*FieldName.ToString(), StartX + X, Y, X, Y, Expected, Actual));
					return false;
				}
			}
		}
		return true;
	}

	bool CompareMountainBulk(FAutomationTestBase& Test, FName Bulk)
	{
		const FEonformTerrainRecipe Recipe = MakeRegionalMountainRecipe(Bulk);
		const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Recipe, FIntPoint(65, 65));
		if (!Support.bSupported)
		{
			Test.AddError(FString::Printf(TEXT("Basic/%s Mountain was not admitted: %s"), *Bulk.ToString(), *Support.Describe()));
			return false;
		}
		if (Support.RequiredBorderSamples != 1)
		{
			Test.AddError(FString::Printf(TEXT("Basic/%s Mountain expected one semantic border sample, got %d"), *Bulk.ToString(), Support.RequiredBorderSamples));
			return false;
		}

		FEonformTerrainEvaluationContext Full = MountainFullContext();
		const FEonformTerrainEvaluationResult FullResult = FEonformTerrainEvaluator::Evaluate(Recipe, Full);
		if (!FullResult.bSuccess)
		{
			Test.AddError(FString::Printf(TEXT("Full Basic/%s Mountain failed: %s"), *Bulk.ToString(), *FullResult.Error));
			return false;
		}

		const FEonformScalarField* FullHeight = FullResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!FullHeight)
		{
			Test.AddError(TEXT("Full Mountain Height is missing."));
			return false;
		}

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
		Right.GlobalSummaryCache = Left.GlobalSummaryCache;

		const FEonformTerrainEvaluationResult LeftResult = FEonformTerrainEvaluator::Evaluate(Recipe, Left);
		const FEonformTerrainEvaluationResult RightResult = FEonformTerrainEvaluator::Evaluate(Recipe, Right);
		if (!LeftResult.bSuccess || !RightResult.bSuccess)
		{
			if (!LeftResult.bSuccess) Test.AddError(FString::Printf(TEXT("Left Basic/%s Mountain failed: %s"), *Bulk.ToString(), *LeftResult.Error));
			if (!RightResult.bSuccess) Test.AddError(FString::Printf(TEXT("Right Basic/%s Mountain failed: %s"), *Bulk.ToString(), *RightResult.Error));
			return false;
		}

		const FName Fields[] = {
			EonformTerrainFieldNames::Height,
			EonformTerrainFieldNames::Elevation,
			EonformTerrainFieldNames::SlopeDegrees,
			EonformTerrainFieldNames::Concavity,
			EonformTerrainFieldNames::Convexity,
			EonformTerrainFieldNames::Mountain,
			EonformTerrainFieldNames::Foothill,
			EonformTerrainFieldNames::Plains,
			EonformTerrainFieldNames::MountainMass,
			EonformTerrainFieldNames::Uplift,
			EonformTerrainFieldNames::RidgeNetwork,
			EonformTerrainFieldNames::DrainageReadiness,
			EonformTerrainFieldNames::ErosionEligibility,
			EonformTerrainFieldNames::RockExposure,
			EonformTerrainFieldNames::CryosphereEligibility
		};

		for (const FName Field : Fields)
		{
			if (!CompareRegionFieldToFull(Test, Field, FullResult.Dataset, LeftResult.Dataset, 0)) return false;
			if (!CompareRegionFieldToFull(Test, Field, FullResult.Dataset, RightResult.Dataset, 32)) return false;

			const FEonformScalarField* LeftField = LeftResult.Dataset.FindScalarField(Field);
			const FEonformScalarField* RightField = RightResult.Dataset.FindScalarField(Field);
			if (!LeftField || !RightField) return false;
			for (int32 Y = 0; Y < 65; ++Y)
			{
				const float LeftSeam = LeftField->AtInterior(32, Y);
				const float RightSeam = RightField->AtInterior(0, Y);
				if (!FMath::IsNearlyEqual(LeftSeam, RightSeam, 1.e-5f))
				{
					Test.AddError(FString::Printf(
						TEXT("Basic/%s Mountain field %s seam differs at row %d: left %.9f right %.9f"),
						*Bulk.ToString(), *Field.ToString(), Y, LeftSeam, RightSeam));
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformMountainRegionalSupportTest,
	"Eonform.Core.RegionalEvaluation.MountainUsesGlobalSemanticContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainRegionalSupportTest::RunTest(const FString& Parameters)
{
	for (const FName Bulk : { FName(TEXT("Low")), FName(TEXT("Medium")), FName(TEXT("High")) })
	{
		const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(
			MakeRegionalMountainRecipe(Bulk),
			FIntPoint(65, 65));
		TestTrue(*FString::Printf(TEXT("Basic/%s Mountain is admitted"), *Bulk.ToString()), Support.bSupported);
		TestEqual(*FString::Printf(TEXT("Basic/%s Mountain requires one semantic dependency sample"), *Bulk.ToString()), Support.RequiredBorderSamples, 1);
	}

	const FEonformTerrainRegionalSupportReport InputSupport = FEonformTerrainRegionalSupport::Analyze(
		MakeInputMultipliedMountainRecipe(),
		FIntPoint(65, 65));
	TestFalse(TEXT("Mountain with a connected In multiplier remains fail-closed"), InputSupport.bSupported);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformMountainRegionalEquivalenceTest,
	"Eonform.Core.RegionalEvaluation.MountainHeightAndSemanticsMatchFullWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainRegionalEquivalenceTest::RunTest(const FString& Parameters)
{
	RegisterEonformTerrainLandformNodes();
	if (!CompareMountainBulk(*this, TEXT("Low"))) return false;
	if (!CompareMountainBulk(*this, TEXT("Medium"))) return false;
	if (!CompareMountainBulk(*this, TEXT("High"))) return false;
	return true;
}

#endif
