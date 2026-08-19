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

	FGaeaTerrainRecipe MakePerlinRecipe(bool bIncludeContext = false)
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(11, 12, 13, 14);
		Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FGaeaTerrainNode ContextNode;
		ContextNode.Id = FGuid(21, 22, 23, 24);
		ContextNode.Type = GaeaTerrainNodeTypes::TerrainContext;

		FGaeaTerrainNode Erosion;
		Erosion.Id = FGuid(15, 16, 17, 18);
		Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

		Recipe.Nodes.Add(Source);
		if (bIncludeContext) Recipe.Nodes.Add(ContextNode);
		Recipe.Nodes.Add(Erosion);

		FGaeaTerrainConnection FirstConnection;
		FirstConnection.FromNode = Source.Id;
		FirstConnection.FromOutput = TEXT("Terrain");
		FirstConnection.ToNode = bIncludeContext ? ContextNode.Id : Erosion.Id;
		FirstConnection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(FirstConnection);

		if (bIncludeContext)
		{
			FGaeaTerrainConnection SecondConnection;
			SecondConnection.FromNode = ContextNode.Id;
			SecondConnection.FromOutput = TEXT("Terrain");
			SecondConnection.ToNode = Erosion.Id;
			SecondConnection.ToInput = TEXT("Terrain");
			Recipe.Connections.Add(SecondConnection);
		}

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
	FGaeaTerrainEvaluatorPerlinTest,
	"CodenameGaea.Core.Graph.PerlinNoiseToHydraulicErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainEvaluatorPerlinTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainRecipe Recipe = MakePerlinRecipe();
	const FGaeaTerrainEvaluationResult First = FGaeaTerrainEvaluator::Evaluate(Recipe, EmptyContext);
	const FGaeaTerrainEvaluationResult Second = FGaeaTerrainEvaluator::Evaluate(Recipe, EmptyContext);

	TestTrue(TEXT("Perlin recipe evaluates without external dataset"), First.bSuccess);
	if (!First.bSuccess)
	{
		AddError(First.Error);
		return false;
	}

	TestEqual(TEXT("Perlin height scale is preserved through erosion"), First.HeightScale, 6400.0f);
	const FGaeaScalarField* Height = First.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Perlin output has Height"), Height);
	if (Height)
	{
		TestEqual(TEXT("Perlin resolution is respected"), Height->Domain.Dimensions, FIntPoint(17, 17));
		TestEqual(TEXT("Perlin sample count is correct"), Height->Values.Num(), 17 * 17);
	}
	TestTrue(TEXT("Perlin output has Wear"), First.Dataset.HasScalarField(GaeaTerrainFieldNames::Wear));
	TestTrue(TEXT("Repeated Perlin evaluation succeeds"), Second.bSuccess);
	if (Height && Second.bSuccess)
	{
		const FGaeaScalarField* SecondHeight = Second.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		TestNotNull(TEXT("Repeated Perlin output has Height"), SecondHeight);
		if (SecondHeight)
		{
			TestEqual(TEXT("Deterministic Perlin values match"), SecondHeight->Values, Height->Values);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainEvaluatorContextTest,
	"CodenameGaea.Core.Graph.PerlinNoiseContextHydraulic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainEvaluatorContextTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakePerlinRecipe(true), EmptyContext);
	TestTrue(TEXT("Perlin context recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("Context pipeline preserves height scale"), Result.HeightScale, 6400.0f);
	TestTrue(TEXT("Context output has Elevation"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Elevation));
	TestTrue(TEXT("Context output has SlopeDegrees"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::SlopeDegrees));
	TestTrue(TEXT("Context output has Concavity"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Concavity));
	TestTrue(TEXT("Context output has Convexity"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Convexity));
	TestTrue(TEXT("Context output has Mountain"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Mountain));
	TestTrue(TEXT("Context output has Foothill"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Foothill));
	TestTrue(TEXT("Context output has Plains"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Plains));
	TestTrue(TEXT("Context fields survive hydraulic erosion"), Result.Dataset.HasScalarField(GaeaTerrainFieldNames::Wear));
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
