#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformScalarField MakeField(
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

	FEonformTerrainEvaluationContext MakeLakeContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(800.0, 800.0, 100.0, 0.0);

		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(9, 9),
			FVector2d(-40000.0, -40000.0),
			FVector2d(40000.0, 40000.0));
		FEonformScalarField Height = MakeField(Domain, EonformTerrainFieldNames::Height, 0.60f);

		// A clear enclosed bowl. The outer terrain is the containment rim; the
		// priority flood therefore resolves the basin surface at 0.60 while the
		// floor ranges from 0.10 to 0.25 (35-50 metres of potential water depth).
		for (int32 Y = 2; Y <= 6; ++Y)
		{
			for (int32 X = 2; X <= 6; ++X)
			{
				const int32 DX = FMath::Abs(X - 4);
				const int32 DY = FMath::Abs(Y - 4);
				const int32 Ring = FMath::Max(DX, DY);
				Height.AtInterior(X, Y) = Ring == 0 ? 0.10f : Ring == 1 ? 0.18f : 0.25f;
			}
		}

		Context.SourceDataset.SetScalarField(MoveTemp(Height));
		return Context;
	}

	FEonformTerrainRecipe MakeLakeRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(501, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		FEonformTerrainNode Lake;
		Lake.Id = FGuid(502, 2, 2, 2);
		Lake.Type = EonformTerrainNodeTypes::Lake;
		Lake.NumericParameters.Add(TEXT("MinimumAreaKm2"), 0.0);
		Lake.NumericParameters.Add(TEXT("MinimumDepthMeters"), 0.01);
		Lake.NumericParameters.Add(TEXT("FillLevel"), 1.0);
		Lake.NumericParameters.Add(TEXT("ShoreWidthMeters"), 150.0);
		Lake.NumericParameters.Add(TEXT("BedCarveMeters"), 0.0);

		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Lake.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Lake };
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Lake.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformLakeClosedBasinTest,
	"Eonform.Core.Graph.LakeClosedBasin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformLakeClosedBasinTest::RunTest(const FString& Parameters)
{
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeLakeRecipe(), MakeLakeContext());
	TestTrue(TEXT("Lake graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Lake = Result.Dataset.FindScalarField(TEXT("Lake"));
	const FEonformScalarField* Depth = Result.Dataset.FindScalarField(TEXT("LakeDepth"));
	const FEonformScalarField* Shore = Result.Dataset.FindScalarField(TEXT("LakeShore"));
	const FEonformScalarField* Surface = Result.Dataset.FindScalarField(TEXT("LakeSurface"));
	TestNotNull(TEXT("Lake mask is published"), Lake);
	TestNotNull(TEXT("Lake depth is published"), Depth);
	TestNotNull(TEXT("Lake shore is published"), Shore);
	TestNotNull(TEXT("Lake surface is published"), Surface);
	if (!Lake || !Depth || !Shore || !Surface) return false;

	TestTrue(TEXT("Basin centre is lake water"), Lake->AtInterior(4, 4) > 0.99f);
	TestTrue(TEXT("Basin centre has physical water depth"), Depth->AtInterior(4, 4) > 40.0f);
	TestTrue(TEXT("Outside rim remains dry"), Lake->AtInterior(0, 0) < 0.01f);
	TestTrue(TEXT("Shore mask exists around the lake boundary"), Shore->AtInterior(1, 4) > 0.0f);
	TestTrue(
		TEXT("Lake surface is level across the basin"),
		FMath::IsNearlyEqual(Surface->AtInterior(4, 4), Surface->AtInterior(3, 4), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformHeightDerivedInvalidationTest,
	"Eonform.Core.TerrainDataset.HeightDerivedInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformHeightDerivedInvalidationTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = FEonformGridDomain::Make(
		FIntPoint(5, 5),
		FVector2d(-200.0, -200.0),
		FVector2d(200.0, 200.0));

	FEonformTerrainDataset Dataset;
	TestTrue(TEXT("Initial Height accepted"), Dataset.SetScalarField(MakeField(Domain, EonformTerrainFieldNames::Height, 0.2f)));
	TestTrue(TEXT("Explicit Lake analysis accepted"), Dataset.SetHeightDerivedScalarField(MakeField(Domain, TEXT("Lake"), 1.0f)));
	TestTrue(TEXT("Intrinsic hydrology accepted"), Dataset.SetScalarField(MakeField(Domain, EonformTerrainFieldNames::FlowAccumulation, 5.0f)));
	TestTrue(TEXT("Authored geology accepted"), Dataset.SetScalarField(MakeField(Domain, EonformTerrainFieldNames::RockHardness, 0.8f)));

	TestTrue(TEXT("Lake is marked Height-derived"), Dataset.IsHeightDerivedScalarField(TEXT("Lake")));
	TestTrue(TEXT("Flow accumulation is intrinsically Height-derived"), Dataset.IsHeightDerivedScalarField(EonformTerrainFieldNames::FlowAccumulation));
	TestFalse(TEXT("Authored geology is not Height-derived"), Dataset.IsHeightDerivedScalarField(EonformTerrainFieldNames::RockHardness));

	TestTrue(TEXT("Replacement Height accepted"), Dataset.SetScalarField(MakeField(Domain, EonformTerrainFieldNames::Height, 0.7f)));
	TestFalse(TEXT("Lake is invalidated by Height replacement"), Dataset.HasScalarField(TEXT("Lake")));
	TestFalse(TEXT("Hydrology is invalidated by Height replacement"), Dataset.HasScalarField(EonformTerrainFieldNames::FlowAccumulation));
	TestTrue(TEXT("Authored geology survives Height replacement"), Dataset.HasScalarField(EonformTerrainFieldNames::RockHardness));
	TestTrue(TEXT("Replacement Height remains present"), Dataset.HasScalarField(EonformTerrainFieldNames::Height));
	return true;
}

#endif
