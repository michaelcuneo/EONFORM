#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformScalarField MakeSeaField(
		const FEonformGridDomain& Domain,
		FName Name,
		float InitialValue = 0.0f,
		EEonformFieldUnit Unit = EEonformFieldUnit::Normalized)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	FEonformTerrainEvaluationContext MakeSeaContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(800.0, 800.0, 100.0, 0.0);

		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(9, 9),
			FVector2d(-40000.0, -40000.0),
			FVector2d(40000.0, 40000.0));
		FEonformScalarField Height = MakeSeaField(Domain, EonformTerrainFieldNames::Height, 0.25f);

		// Boundary-connected ocean on the west side. At a 100 m elevation scale,
		// -0.20 represents a 20 m marine bed below the fixed 0 m sea-level datum.
		for (int32 Y = 0; Y < 9; ++Y)
		{
			Height.AtInterior(0, Y) = -0.20f;
			Height.AtInterior(1, Y) = -0.16f;
			Height.AtInterior(2, Y) = -0.08f;
		}

		// A separate negative inland depression. It is below sea level but is
		// surrounded by positive terrain, so Sea must not classify it as ocean.
		Height.AtInterior(6, 4) = -0.30f;

		Context.SourceDataset.SetScalarField(MoveTemp(Height));
		return Context;
	}

	FEonformTerrainRecipe MakeSeaRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(601, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		FEonformTerrainNode Sea;
		Sea.Id = FGuid(602, 2, 2, 2);
		Sea.Type = EonformTerrainNodeTypes::Sea;
		Sea.NumericParameters.Add(TEXT("ShoreWidthMeters"), 150.0);
		Sea.NumericParameters.Add(TEXT("ShelfWidthMeters"), 0.0);
		Sea.NumericParameters.Add(TEXT("ShelfDepthMeters"), 0.0);
		Sea.NumericParameters.Add(TEXT("CoastalErosionMeters"), 0.0);
		Sea.NumericParameters.Add(TEXT("BeachDepositionMeters"), 0.0);

		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Sea.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Sea };
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Sea.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformSeaBoundaryConnectivityTest,
	"Eonform.Core.Graph.SeaBoundaryConnectivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformSeaBoundaryConnectivityTest::RunTest(const FString& Parameters)
{
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeSeaRecipe(), MakeSeaContext());
	TestTrue(TEXT("Sea graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Sea = Result.Dataset.FindScalarField(TEXT("Sea"));
	const FEonformScalarField* Depth = Result.Dataset.FindScalarField(TEXT("SeaDepth"));
	const FEonformScalarField* Shore = Result.Dataset.FindScalarField(TEXT("SeaShore"));
	const FEonformScalarField* Surface = Result.Dataset.FindScalarField(TEXT("SeaSurface"));
	TestNotNull(TEXT("Sea mask is published"), Sea);
	TestNotNull(TEXT("Sea depth is published"), Depth);
	TestNotNull(TEXT("Sea shore is published"), Shore);
	TestNotNull(TEXT("Sea surface is published"), Surface);
	if (!Sea || !Depth || !Shore || !Surface) return false;

	TestTrue(TEXT("Boundary-connected negative terrain is ocean"), Sea->AtInterior(0, 4) > 0.99f);
	TestTrue(TEXT("Ocean depth is expressed in metres"), Depth->AtInterior(0, 4) > 19.0f);
	TestTrue(TEXT("Enclosed negative inland basin is not ocean"), Sea->AtInterior(6, 4) < 0.01f);
	TestTrue(TEXT("Land immediately beside the ocean receives shoreline influence"), Shore->AtInterior(3, 4) > 0.0f);
	TestTrue(TEXT("Sea surface is fixed at the zero metre datum"), FMath::IsNearlyZero(Surface->AtInterior(0, 4), 0.001f));
	return true;
}

#endif
