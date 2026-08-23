#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaScalarField MakeEcologyHeight(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor D;
		D.Name = GaeaTerrainFieldNames::Height;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
		Height.Initialize(Domain, D, 0.0f);
		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const float NX = static_cast<float>(X) / static_cast<float>(Domain.Dimensions.X - 1);
				const float NY = static_cast<float>(Y) / static_cast<float>(Domain.Dimensions.Y - 1);
				const float Valley = 0.12f * FMath::Abs(NX - 0.5f);
				const float Relief = 0.22f * NY + 0.06f * FMath::Sin(NX * UE_TWO_PI);
				Height.AtInterior(X, Y) = FMath::Clamp(0.18f + Relief + Valley, 0.05f, 0.85f);
			}
		}
		return Height;
	}

	FGaeaTerrainEvaluationContext MakeEcologyContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(2400.0, 2400.0, 1200.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(17, 17),
			FVector2d(-120000.0, -120000.0),
			FVector2d(120000.0, 120000.0));
		Context.SourceDataset.SetScalarField(MakeEcologyHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeEcologyRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(1001, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		// Explicit habitat ranges keep this regression test focused on ecological
		// differentiation instead of relying on whichever defaults happen to match
		// this small synthetic terrain. Trees prefer the gentler ground; shrubs are
		// intentionally tolerant of much steeper and poorer habitat.
		FGaeaTerrainNode Trees;
		Trees.Id = FGuid(1002, 2, 2, 2);
		Trees.Type = GaeaTerrainNodeTypes::Trees;
		Trees.NumericParameters.Add(TEXT("Density"), 1.0);
		Trees.NumericParameters.Add(TEXT("MoisturePreference"), 0.45);
		Trees.NumericParameters.Add(TEXT("SoilPreference"), 0.15);
		Trees.NumericParameters.Add(TEXT("MaxSlopeDegrees"), 18.0);
		Trees.NumericParameters.Add(TEXT("ShelterStrength"), 0.25);
		Trees.NumericParameters.Add(TEXT("RiparianStrength"), 0.35);

		FGaeaTerrainNode Shrubs;
		Shrubs.Id = FGuid(1003, 3, 3, 3);
		Shrubs.Type = GaeaTerrainNodeTypes::Shrubs;
		Shrubs.NumericParameters.Add(TEXT("Density"), 1.0);
		Shrubs.NumericParameters.Add(TEXT("MoisturePreference"), 0.40);
		Shrubs.NumericParameters.Add(TEXT("SoilPreference"), 0.08);
		Shrubs.NumericParameters.Add(TEXT("MaxSlopeDegrees"), 65.0);
		Shrubs.NumericParameters.Add(TEXT("ShelterStrength"), 0.10);
		Shrubs.NumericParameters.Add(TEXT("RiparianStrength"), 0.20);

		FGaeaTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Terrain");
		A.ToNode = Trees.Id;
		A.ToInput = TEXT("Terrain");

		FGaeaTerrainConnection B;
		B.FromNode = Trees.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Shrubs.Id;
		B.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Trees, Shrubs };
		Recipe.Connections = { A, B };
		Recipe.OutputNode = Shrubs.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaEcologySuitabilityTest,
	"CodenameGaea.Core.Graph.EcologySuitability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaEcologySuitabilityTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor TreesDescriptor;
	FGaeaTerrainNodeDescriptor ShrubsDescriptor;
	TestTrue(TEXT("Trees descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Trees, TreesDescriptor));
	TestTrue(TEXT("Shrubs descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Shrubs, ShrubsDescriptor));
	TestEqual(TEXT("Trees parameter contract"), TreesDescriptor.Parameters.Num(), 9);
	TestEqual(TEXT("Shrubs parameter contract"), ShrubsDescriptor.Parameters.Num(), 9);

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeEcologyRecipe(), MakeEcologyContext());
	TestTrue(TEXT("Ecology graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Trees = Result.Dataset.FindScalarField(TEXT("Trees"));
	const FGaeaScalarField* TreeSuitability = Result.Dataset.FindScalarField(TEXT("TreeSuitability"));
	const FGaeaScalarField* Shrubs = Result.Dataset.FindScalarField(TEXT("Shrubs"));
	const FGaeaScalarField* ShrubSuitability = Result.Dataset.FindScalarField(TEXT("ShrubSuitability"));
	const FGaeaScalarField* Moisture = Result.Dataset.FindScalarField(TEXT("VegetationMoisture"));
	TestNotNull(TEXT("Trees density is published"), Trees);
	TestNotNull(TEXT("Tree suitability is published"), TreeSuitability);
	TestNotNull(TEXT("Shrubs density is published"), Shrubs);
	TestNotNull(TEXT("Shrub suitability is published"), ShrubSuitability);
	TestNotNull(TEXT("Vegetation moisture is published"), Moisture);
	if (!Trees || !TreeSuitability || !Shrubs || !ShrubSuitability || !Moisture) return false;

	float MaxTrees = 0.0f;
	float MaxShrubs = 0.0f;
	bool bShrubOnlyHabitat = false;
	for (int32 I = 0; I < Trees->Values.Num(); ++I)
	{
		TestTrue(TEXT("Tree density remains finite"), FMath::IsFinite(Trees->Values[I]));
		TestTrue(TEXT("Shrub density remains finite"), FMath::IsFinite(Shrubs->Values[I]));
		MaxTrees = FMath::Max(MaxTrees, Trees->Values[I]);
		MaxShrubs = FMath::Max(MaxShrubs, Shrubs->Values[I]);
		if (ShrubSuitability->Values[I] > 0.1f && TreeSuitability->Values[I] < 0.05f) bShrubOnlyHabitat = true;
	}

	TestTrue(TEXT("Terrain produces viable tree habitat"), MaxTrees > 0.05f);
	TestTrue(TEXT("Terrain produces viable shrub habitat"), MaxShrubs > 0.05f);
	TestTrue(TEXT("Shrubs can occupy harsher habitat than trees"), bShrubOnlyHabitat);
	return true;
}

#endif
