#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	void ConnectDerivedDataTestNodes(FGaeaTerrainRecipe& Recipe, const FGuid& From, const FGuid& To)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = To;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
	}

	FGaeaTerrainRecipe MakeImplicitAnalysisRecipe()
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(201, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FGaeaTerrainNode Shape;
		Shape.Id = FGuid(202, 2, 2, 2);
		Shape.Type = GaeaTerrainNodeTypes::TerrainShape;
		Shape.IntegerParameters.Add(TEXT("Seed"), 42);

		FGaeaTerrainNode Erosion;
		Erosion.Id = FGuid(203, 3, 3, 3);
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

		Recipe.Nodes = { Source, Shape, Erosion };
		ConnectDerivedDataTestNodes(Recipe, Source.Id, Shape.Id);
		ConnectDerivedDataTestNodes(Recipe, Shape.Id, Erosion.Id);
		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainImplicitAnalysisGraphTest,
	"CodenameGaea.Core.Graph.HydraulicDerivesRequiredAnalysisOnDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainImplicitAnalysisGraphTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeImplicitAnalysisRecipe(), EmptyContext);
	TestTrue(TEXT("Shaped terrain evaluates directly through hydraulic erosion"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FName ExpectedFields[] =
	{
		GaeaTerrainFieldNames::Height,
		GaeaTerrainFieldNames::Elevation,
		GaeaTerrainFieldNames::SlopeDegrees,
		GaeaTerrainFieldNames::Concavity,
		GaeaTerrainFieldNames::Convexity,
		GaeaTerrainFieldNames::Mountain,
		GaeaTerrainFieldNames::Foothill,
		GaeaTerrainFieldNames::Plains,
		GaeaTerrainFieldNames::Thermal,
		GaeaTerrainFieldNames::Rainfall,
		GaeaTerrainFieldNames::HydraulicErosion,
		GaeaTerrainFieldNames::Deposition,
		GaeaTerrainFieldNames::Evaporation,
		GaeaTerrainFieldNames::RockHardness,
		GaeaTerrainFieldNames::Weathering,
		GaeaTerrainFieldNames::SoilDepth,
		GaeaTerrainFieldNames::Wear,
		GaeaTerrainFieldNames::Deposits,
		GaeaTerrainFieldNames::Flow
	};

	for (const FName FieldName : ExpectedFields)
	{
		TestTrue(
			*FString::Printf(TEXT("Output contains %s"), *FieldName.ToString()),
			Result.Dataset.HasScalarField(FieldName));
	}

	const FName LazyHydrologyFields[] =
	{
		GaeaTerrainFieldNames::FlowDirection,
		GaeaTerrainFieldNames::FlowAccumulation,
		GaeaTerrainFieldNames::CatchmentAreaKm2,
		GaeaTerrainFieldNames::DistanceToOutletKm,
		GaeaTerrainFieldNames::StreamOrder
	};
	for (const FName FieldName : LazyHydrologyFields)
	{
		TestFalse(
			*FString::Printf(TEXT("Hydraulic erosion does not eagerly derive %s"), *FieldName.ToString()),
			Result.Dataset.HasScalarField(FieldName));
	}

	TestEqual(TEXT("Hydraulic output publishes only required canonical fields"), Result.Dataset.NumScalarFields(), 19);
	return true;
}

#endif
