#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	void Connect(FEonformTerrainRecipe& Recipe, const FGuid& From, const FGuid& To)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = From;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = To;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
	}

	FEonformTerrainRecipe MakeGeologyRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(101, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FEonformTerrainNode Shape;
		Shape.Id = FGuid(102, 2, 2, 2);
		Shape.Type = EonformTerrainNodeTypes::TerrainShape;
		Shape.IntegerParameters.Add(TEXT("Seed"), 42);

		FEonformTerrainNode Context;
		Context.Id = FGuid(103, 3, 3, 3);
		Context.Type = EonformTerrainNodeTypes::TerrainContext;

		FEonformTerrainNode Geology;
		Geology.Id = FGuid(104, 4, 4, 4);
		Geology.Type = EonformTerrainNodeTypes::Geology;
		Geology.IntegerParameters.Add(TEXT("Seed"), 42);

		FEonformTerrainNode Masks;
		Masks.Id = FGuid(105, 5, 5, 5);
		Masks.Type = EonformTerrainNodeTypes::ProcessMasks;

		FEonformTerrainNode Erosion;
		Erosion.Id = FGuid(106, 6, 6, 6);
		Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
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
	FEonformTerrainGeologyGraphTest,
	"Eonform.Core.Graph.PerlinNoiseShapeContextGeologyMasksHydraulic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainGeologyGraphTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext EmptyContext;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeGeologyRecipe(), EmptyContext);
	TestTrue(TEXT("Full geology recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestTrue(TEXT("Output has RockHardness"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::RockHardness));
	TestTrue(TEXT("Output has Weathering"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Weathering));
	TestTrue(TEXT("Output has SoilDepth"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::SoilDepth));
	TestTrue(TEXT("Output preserves Rainfall mask"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Rainfall));
	TestTrue(TEXT("Output preserves HydraulicErosion mask"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::HydraulicErosion));
	TestTrue(TEXT("Output has Wear"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Wear));
	TestTrue(TEXT("Output has Deposits"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Deposits));
	TestTrue(TEXT("Output has Flow"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Flow));

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* Hardness = Result.Dataset.FindScalarField(EonformTerrainFieldNames::RockHardness);
	const FEonformScalarField* Soil = Result.Dataset.FindScalarField(EonformTerrainFieldNames::SoilDepth);
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
