#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaScalarField MakeSnowField(const FGaeaGridDomain& Domain, FName Name, float InitialValue = 0.0f)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	FGaeaTerrainEvaluationContext MakeSnowContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(800.0, 800.0, 1000.0, 0.0);

		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(9, 9),
			FVector2d(-40000.0, -40000.0),
			FVector2d(40000.0, 40000.0));
		FGaeaScalarField Height = MakeSnowField(Domain, GaeaTerrainFieldNames::Height, 0.0f);

		for (int32 Y = 0; Y < 9; ++Y)
		{
			for (int32 X = 0; X < 9; ++X)
			{
				const float DX = static_cast<float>(X - 4);
				const float DY = static_cast<float>(Y - 4);
				const float Radius = FMath::Sqrt(DX * DX + DY * DY);
				Height.AtInterior(X, Y) = FMath::Clamp(0.85f - Radius * 0.12f, 0.05f, 0.85f);
			}
		}

		Context.SourceDataset.SetScalarField(MoveTemp(Height));
		return Context;
	}

	FGaeaTerrainRecipe MakeSnowRecipe()
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(701, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode Snow;
		Snow.Id = FGuid(702, 2, 2, 2);
		Snow.Type = FName(TEXT("Snow"));
		Snow.NumericParameters.Add(TEXT("BaseTemperatureC"), 4.0);
		Snow.NumericParameters.Add(TEXT("LapseRateCPerKm"), 8.0);
		Snow.NumericParameters.Add(TEXT("SnowTemperatureC"), 1.5);
		Snow.NumericParameters.Add(TEXT("Precipitation"), 1.0);
		Snow.NumericParameters.Add(TEXT("MaxDepthMeters"), 2.0);
		Snow.BoolParameters.Add(TEXT("AffectHeight"), true);

		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Snow.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Snow };
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Snow.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaSnowPhysicalAccumulationTest,
	"CodenameGaea.Core.Graph.SnowPhysicalAccumulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSnowPhysicalAccumulationTest::RunTest(const FString& Parameters)
{
	const FGaeaTerrainEvaluationContext Context = MakeSnowContext();
	const FGaeaScalarField* OriginalHeight = Context.SourceDataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const float OriginalPeak = OriginalHeight ? OriginalHeight->AtInterior(4, 4) : 0.0f;

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeSnowRecipe(), Context);
	TestTrue(TEXT("Snow graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Snow = Result.Dataset.FindScalarField(TEXT("Snow"));
	const FGaeaScalarField* Depth = Result.Dataset.FindScalarField(TEXT("SnowDepth"));
	const FGaeaScalarField* Temperature = Result.Dataset.FindScalarField(TEXT("TemperatureC"));
	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Snow mask is published"), Snow);
	TestNotNull(TEXT("Snow depth is published"), Depth);
	TestNotNull(TEXT("Temperature is published"), Temperature);
	TestNotNull(TEXT("Height remains published"), Height);
	if (!Snow || !Depth || !Temperature || !Height) return false;

	TestTrue(TEXT("Cold high terrain accumulates snow"), Snow->AtInterior(4, 4) > 0.25f);
	TestTrue(TEXT("Snow depth is expressed in metres"), Depth->AtInterior(4, 4) > 0.1f);
	TestTrue(TEXT("High terrain is colder than low terrain"), Temperature->AtInterior(4, 4) < Temperature->AtInterior(0, 0));
	TestTrue(TEXT("Accumulated snow can raise the physical terrain surface"), Height->AtInterior(4, 4) > OriginalPeak);
	return true;
}

#endif
