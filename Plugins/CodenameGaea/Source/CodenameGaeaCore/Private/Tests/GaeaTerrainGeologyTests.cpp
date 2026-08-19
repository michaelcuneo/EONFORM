#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	void Connect(FGaeaTerrainRecipe& Recipe, const FGuid& From, const FGuid& To)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = To;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
	}

	FGaeaTerrainRecipe MakeGeologyRecipe()
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(101, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FGaeaTerrainNode Shape;
		Shape.Id = FGuid(102, 2, 2, 2);
		Shape.Type = GaeaTerrainNodeTypes::TerrainShape;
		Shape.IntegerParameters.Add(TEXT("Seed"), 42);

		FGaeaTerrainNode Context;
		Context.Id = FGuid(103, 3, 3, 3);
		Context.Type = GaeaTerrainNodeTypes::TerrainContext;

		FGaeaTerrainNode Geology;
		Geology.Id = FGuid(104, 4, 4, 4);
		Geology.Type = GaeaTerrainNodeTypes::Geology;
		Geology.IntegerParameters.Add(TEXT("Seed"), 42);

		FGaeaTerrainNode Masks;
		Masks.Id = FGuid(105, 5, 5, 5);
		Masks.Type = GaeaTerrainNodeTypes::ProcessMasks;

		FGaeaTerrainNode Erosion;
		Erosion.Id = FGuid(106, 6, 6, 6);
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

		Recipe.Nodes = { Source, Shape, Context, Geology, Masks, Erosion };
		Connect(Recipe, Source.Id, Shape.Id);
		Connect(Recipe, Shape.Id, Context.Id);
		Connect(Recipe, Context.Id, Geology.Id);
		Connect(Recipe, Geology.Id, Masks.Id);
		Connect(Recipe, Masks.Id, Erosion.Id);
		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainGeologyGraphTest,
	"CodenameGaea.Core.Graph.PerlinNoiseShapeContextGeologyMasksHydraulic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainGeologyGraphTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeGeologyRecipe(), EmptyContext);
	TestTrue(TEXT("Full geology recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestTrue(TEXT("Output has RockHardness"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::RockHardness));
	TestTrue(TEXT("Output has Weathering"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Weathering));
	TestTrue(TEXT("Output has SoilDepth"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::SoilDepth));
	TestTrue(TEXT("Output preserves Rainfall mask"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Rainfall));
	TestTrue(TEXT("Output preserves HydraulicErosion mask"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::HydraulicErosion));
	TestTrue(TEXT("Output has Wear"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Wear));
	TestTrue(TEXT("Output has Deposits"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Deposits));
	TestTrue(TEXT("Output has Flow"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Flow));

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* Hardness = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness);
	const FGaeaScalarField* Soil = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
	TestNotNull(TEXT("Height exists"), Height);
	TestNotNull(TEXT("RockHardness exists"), Hardness);
	TestNotNull(TEXT("SoilDepth exists"), Soil);
	if (Height && Hardness && Soil)
	{
		TestEqual(TEXT("RockHardness domain matches Height"), Hardness->Domain, Height->Domain);
		TestEqual(TEXT("SoilDepth domain matches Height"), Soil->Domain, Height->Domain);
	}
	return true;
}

#endif
