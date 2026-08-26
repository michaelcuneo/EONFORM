#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	void ConnectDerivedDataTestNodes(FEonformTerrainRecipe& Recipe, const FGuid& From, const FGuid& To)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = From;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = To;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
	}

	FEonformTerrainRecipe MakeImplicitAnalysisRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(201, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FEonformTerrainNode Shape;
		Shape.Id = FGuid(202, 2, 2, 2);
		Shape.Type = EonformTerrainNodeTypes::TerrainShape;
		Shape.IntegerParameters.Add(TEXT("Seed"), 42);

		FEonformTerrainNode Erosion;
		Erosion.Id = FGuid(203, 3, 3, 3);
		Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

		Recipe.Nodes = { Source, Shape, Erosion };
		ConnectDerivedDataTestNodes(Recipe, Source.Id, Shape.Id);
		ConnectDerivedDataTestNodes(Recipe, Shape.Id, Erosion.Id);
		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainImplicitAnalysisGraphTest,
	"Eonform.Core.Graph.HydraulicDerivesRequiredAnalysisOnDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainImplicitAnalysisGraphTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext EmptyContext;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeImplicitAnalysisRecipe(), EmptyContext);
	TestTrue(TEXT("Shaped terrain evaluates directly through hydraulic erosion"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FName ExpectedFields[] =
	{
		EonformTerrainFieldNames::Height,
		EonformTerrainFieldNames::Elevation,
		EonformTerrainFieldNames::SlopeDegrees,
		EonformTerrainFieldNames::Concavity,
		EonformTerrainFieldNames::Convexity,
		EonformTerrainFieldNames::Mountain,
		EonformTerrainFieldNames::Foothill,
		EonformTerrainFieldNames::Plains,
		EonformTerrainFieldNames::Thermal,
		EonformTerrainFieldNames::Rainfall,
		EonformTerrainFieldNames::HydraulicErosion,
		EonformTerrainFieldNames::Deposition,
		EonformTerrainFieldNames::Evaporation,
		EonformTerrainFieldNames::RockHardness,
		EonformTerrainFieldNames::Weathering,
		EonformTerrainFieldNames::SoilDepth,
		EonformTerrainFieldNames::Wear,
		EonformTerrainFieldNames::Deposits,
		EonformTerrainFieldNames::Flow
	};

	for (const FName FieldName : ExpectedFields)
	{
		TestTrue(
			*FString::Printf(TEXT("Output contains %s"), *FieldName.ToString()),
			Result.Dataset.HasScalarField(FieldName));
	}

	const FName LazyHydrologyFields[] =
	{
		EonformTerrainFieldNames::FlowDirection,
		EonformTerrainFieldNames::FlowAccumulation,
		EonformTerrainFieldNames::CatchmentAreaKm2,
		EonformTerrainFieldNames::DistanceToOutletKm,
		EonformTerrainFieldNames::StreamOrder
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
