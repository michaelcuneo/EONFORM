#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaTerrainDataset MakeSourceDataset()
	{
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(9, 9), FVector2d(-400.0, -400.0), FVector2d(400.0, 400.0));
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor);
		for (int32 Y = 0; Y < 9; ++Y)
		{
			for (int32 X = 0; X < 9; ++X)
			{
				const float NX = static_cast<float>(X) / 8.0f;
				const float NY = static_cast<float>(Y) / 8.0f;
				Height.AtInterior(X, Y) = 0.2f * NX + 0.35f * FMath::Max(0.0f, 1.0f - FVector2D(NX - 0.5f, NY - 0.5f).Size() * 2.0f);
			}
		}
		FGaeaTerrainDataset Dataset;
		Dataset.SetScalarField(MoveTemp(Height));
		return Dataset;
	}

	FGaeaTerrainRecipe MakeRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(1, 2, 3, 4);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;
		FGaeaTerrainNode Erosion;
		Erosion.Id = FGuid(5, 6, 7, 8);
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 4);
		Erosion.NumericParameters.Add(TEXT("Rainfall"), 0.02);
		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Erosion);
		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Erosion.Id;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}

	FGaeaTerrainRecipe MakeProceduralRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(11, 12, 13, 14);
		Source.Type = GaeaTerrainNodeTypes::ProceduralTerrain;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FGaeaTerrainNode Erosion;
		Erosion.Id = FGuid(15, 16, 17, 18);
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Erosion);
		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Erosion.Id;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainRecipeValidationTest,
	"CodenameGaea.Core.Graph.RecipeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainRecipeValidationTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe = MakeRecipe();
	FString Error;
	TestTrue(TEXT("Valid recipe passes validation"), Recipe.Validate(&Error));
	const uint32 FirstHash = Recipe.GetDeterministicHash();
	Recipe.Nodes.Swap(0, 1);
	TestEqual(TEXT("Node ordering does not affect recipe hash"), Recipe.GetDeterministicHash(), FirstHash);

	FGaeaTerrainConnection Duplicate = Recipe.Connections[0];
	Duplicate.FromNode = Recipe.OutputNode;
	Recipe.Connections.Add(Duplicate);
	TestFalse(TEXT("Duplicate target input is rejected"), Recipe.Validate(&Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainEvaluatorHydraulicTest,
	"CodenameGaea.Core.Graph.SourceToHydraulicErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainEvaluatorHydraulicTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext Context;
	Context.SourceDataset = MakeSourceDataset();
	Context.HeightScale = 1000.0f;
	const TArray<float> OriginalHeight = Context.SourceDataset.FindScalarField(GaeaTerrainFieldNames::Height)->Values;

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeRecipe(), Context);
	TestTrue(TEXT("Recipe evaluates successfully"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("External source height scale is preserved"), Result.HeightScale, Context.HeightScale);
	TestTrue(TEXT("Output has Height"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Height));
	TestTrue(TEXT("Output has Wear"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Wear));
	TestTrue(TEXT("Output has Deposits"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Deposits));
	TestTrue(TEXT("Output has Flow"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Flow));

	const TArray<float>& CurrentHeight = Context.SourceDataset.FindScalarField(GaeaTerrainFieldNames::Height)->Values;
	TestEqual(TEXT("Source sample count remains unchanged"), CurrentHeight.Num(), OriginalHeight.Num());
	for (int32 Index = 0; Index < CurrentHeight.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(CurrentHeight[Index], OriginalHeight[Index]))
		{
			AddError(FString::Printf(TEXT("Graph evaluation mutated source height at index %d"), Index));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainEvaluatorProceduralTest,
	"CodenameGaea.Core.Graph.ProceduralTerrainToHydraulicErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainEvaluatorProceduralTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainRecipe Recipe = MakeProceduralRecipe();
	const FGaeaTerrainEvaluationResult First = FGaeaTerrainEvaluator::Evaluate(Recipe, EmptyContext);
	const FGaeaTerrainEvaluationResult Second = FGaeaTerrainEvaluator::Evaluate(Recipe, EmptyContext);

	TestTrue(TEXT("Procedural recipe evaluates without external dataset"), First.bSuccess);
	if (!First.bSuccess)
	{
		AddError(First.Error);
		return false;
	}

	TestEqual(TEXT("Procedural height scale is preserved through erosion"), First.HeightScale, 6400.0f);
	const FGaeaScalarField* Height = First.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Procedural output has Height"), Height);
	if (Height)
	{
		TestEqual(TEXT("Procedural resolution is respected"), Height->Domain.Dimensions, FIntPoint(17, 17));
		TestEqual(TEXT("Procedural sample count is correct"), Height->Values.Num(), 17 * 17);
	}
	TestTrue(TEXT("Procedural output has Wear"), First.Dataset.HasScalarField(GaeaTerrainFieldNames::Wear));
	TestTrue(TEXT("Repeated procedural evaluation succeeds"), Second.bSuccess);
	if (Height && Second.bSuccess)
	{
		const FGaeaScalarField* SecondHeight = Second.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		TestNotNull(TEXT("Repeated procedural output has Height"), SecondHeight);
		if (SecondHeight)
		{
			TestEqual(TEXT("Deterministic procedural values match"), SecondHeight->Values, Height->Values);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainEvaluatorCycleTest,
	"CodenameGaea.Core.Graph.CycleDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainEvaluatorCycleTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe = MakeRecipe();
	FGaeaTerrainConnection BackEdge;
	BackEdge.FromNode = Recipe.OutputNode;
	BackEdge.FromOutput = TEXT("Terrain");
	BackEdge.ToNode = FGuid(1, 2, 3, 4);
	BackEdge.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(BackEdge);

	FGaeaTerrainEvaluationContext Context;
	Context.SourceDataset = MakeSourceDataset();
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestFalse(TEXT("Cyclic recipe fails"), Result.bSuccess);
	TestTrue(TEXT("Cycle error is reported"), Result.Error.Contains(TEXT("cycle"), ESearchCase::IgnoreCase));
	return true;
}

#endif
