#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformScalarField MakeSnowField(const FEonformGridDomain& Domain, FName Name, float InitialValue = 0.0f)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	FEonformTerrainEvaluationContext MakeSnowContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(800.0, 800.0, 1000.0, 0.0);

		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(9, 9),
			FVector2d(-40000.0, -40000.0),
			FVector2d(40000.0, 40000.0));
		FEonformScalarField Height = MakeSnowField(Domain, EonformTerrainFieldNames::Height, 0.0f);

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

	FEonformTerrainRecipe MakeSnowRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(701, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		FEonformTerrainNode Snow;
		Snow.Id = FGuid(702, 2, 2, 2);
		Snow.Type = FName(TEXT("Snow"));
		Snow.NumericParameters.Add(TEXT("BaseTemperatureC"), 4.0);
		Snow.NumericParameters.Add(TEXT("LapseRateCPerKm"), 8.0);
		Snow.NumericParameters.Add(TEXT("SnowTemperatureC"), 1.5);
		Snow.NumericParameters.Add(TEXT("Precipitation"), 1.0);
		Snow.NumericParameters.Add(TEXT("MaxDepthMeters"), 2.0);
		Snow.BoolParameters.Add(TEXT("AffectHeight"), true);

		FEonformTerrainConnection Connection;
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
	FEonformSnowPhysicalAccumulationTest,
	"Eonform.Core.Graph.SnowPhysicalAccumulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformSnowPhysicalAccumulationTest::RunTest(const FString& Parameters)
{
	const FEonformTerrainEvaluationContext Context = MakeSnowContext();
	const FEonformScalarField* OriginalHeight = Context.SourceDataset.FindScalarField(EonformTerrainFieldNames::Height);
	const float OriginalPeak = OriginalHeight ? OriginalHeight->AtInterior(4, 4) : 0.0f;

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeSnowRecipe(), Context);
	TestTrue(TEXT("Snow graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Snow = Result.Dataset.FindScalarField(TEXT("Snow"));
	const FEonformScalarField* Depth = Result.Dataset.FindScalarField(TEXT("SnowDepth"));
	const FEonformScalarField* Temperature = Result.Dataset.FindScalarField(TEXT("TemperatureC"));
	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
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
