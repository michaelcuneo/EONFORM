#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainDataset MakeSourceDataset()
	{
		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(9, 9), FVector2d(-400.0, -400.0), FVector2d(400.0, 400.0));
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		FEonformScalarField Height;
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
		FEonformTerrainDataset Dataset;
		Dataset.SetScalarField(MoveTemp(Height));
		return Dataset;
	}

	FEonformTerrainRecipe MakeRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source;
		Source.Id = FGuid(1, 2, 3, 4);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;
		FEonformTerrainNode Erosion;
		Erosion.Id = FGuid(5, 6, 7, 8);
		Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 4);
		Erosion.NumericParameters.Add(TEXT("Rainfall"), 0.02);
		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Erosion);
		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Erosion.Id;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Erosion.Id;
		return Recipe;
	}

	FEonformTerrainRecipe MakePerlinRecipe(bool bIncludeContext = false)
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source;
		Source.Id = FGuid(11, 12, 13, 14);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 17);
		Source.IntegerParameters.Add(TEXT("Seed"), 42);
		Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

		FEonformTerrainNode ContextNode;
		ContextNode.Id = FGuid(21, 22, 23, 24);
		ContextNode.Type = EonformTerrainNodeTypes::TerrainContext;

		FEonformTerrainNode Erosion;
		Erosion.Id = FGuid(15, 16, 17, 18);
		Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
		Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

		Recipe.Nodes.Add(Source);
		if (bIncludeContext) Recipe.Nodes.Add(ContextNode);
		Recipe.Nodes.Add(Erosion);

		FEonformTerrainConnection FirstConnection;
		FirstConnection.FromNode = Source.Id;
		FirstConnection.FromOutput = TEXT("Terrain");
		FirstConnection.ToNode = bIncludeContext ? ContextNode.Id : Erosion.Id;
		FirstConnection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(FirstConnection);

		if (bIncludeContext)
		{
			FEonformTerrainConnection SecondConnection;
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
	FEonformTerrainRecipeValidationTest,
	"Eonform.Core.Graph.RecipeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainRecipeValidationTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe = MakeRecipe();
	FString Error;
	TestTrue(TEXT("Valid recipe passes validation"), Recipe.Validate(&Error));
	const uint32 FirstHash = Recipe.GetDeterministicHash();
	Recipe.Nodes.Swap(0, 1);
	TestEqual(TEXT("Node ordering does not affect recipe hash"), Recipe.GetDeterministicHash(), FirstHash);

	FEonformTerrainConnection Duplicate = Recipe.Connections[0];
	Duplicate.FromNode = Recipe.OutputNode;
	Recipe.Connections.Add(Duplicate);
	TestFalse(TEXT("Duplicate target input is rejected"), Recipe.Validate(&Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainEvaluatorHydraulicTest,
	"Eonform.Core.Graph.SourceToHydraulicErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainEvaluatorHydraulicTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext Context;
	Context.SourceDataset = MakeSourceDataset();
	Context.HeightScale = 1000.0f;
	const TArray<float> OriginalHeight = Context.SourceDataset.FindScalarField(EonformTerrainFieldNames::Height)->Values;

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeRecipe(), Context);
	TestTrue(TEXT("Recipe evaluates successfully"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("External source height scale is preserved"), Result.HeightScale, Context.HeightScale);
	TestTrue(TEXT("Output has Height"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Height));
	TestTrue(TEXT("Output has Wear"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Wear));
	TestTrue(TEXT("Output has Deposits"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Deposits));
	TestTrue(TEXT("Output has Flow"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Flow));

	const TArray<float>& CurrentHeight = Context.SourceDataset.FindScalarField(EonformTerrainFieldNames::Height)->Values;
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
	FEonformTerrainEvaluatorPerlinTest,
	"Eonform.Core.Graph.PerlinNoiseToHydraulicErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainEvaluatorPerlinTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext EmptyContext;
	const FEonformTerrainRecipe Recipe = MakePerlinRecipe();
	const FEonformTerrainEvaluationResult First = FEonformTerrainEvaluator::Evaluate(Recipe, EmptyContext);
	const FEonformTerrainEvaluationResult Second = FEonformTerrainEvaluator::Evaluate(Recipe, EmptyContext);

	TestTrue(TEXT("Perlin recipe evaluates without external dataset"), First.bSuccess);
	if (!First.bSuccess)
	{
		AddError(First.Error);
		return false;
	}

	TestEqual(TEXT("Perlin height scale is preserved through erosion"), First.HeightScale, 6400.0f);
	const FEonformScalarField* Height = First.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Perlin output has Height"), Height);
	if (Height)
	{
		TestEqual(TEXT("Perlin resolution is respected"), Height->Domain.Dimensions, FIntPoint(17, 17));
		TestEqual(TEXT("Perlin sample count is correct"), Height->Values.Num(), 17 * 17);
	}
	TestTrue(TEXT("Perlin output has Wear"), First.Dataset.HasScalarField(EonformTerrainFieldNames::Wear));
	TestTrue(TEXT("Repeated Perlin evaluation succeeds"), Second.bSuccess);
	if (Height && Second.bSuccess)
	{
		const FEonformScalarField* SecondHeight = Second.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		TestNotNull(TEXT("Repeated Perlin output has Height"), SecondHeight);
		if (SecondHeight)
		{
			TestEqual(TEXT("Deterministic Perlin values match"), SecondHeight->Values, Height->Values);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainEvaluatorContextTest,
	"Eonform.Core.Graph.PerlinNoiseContextHydraulic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainEvaluatorContextTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext EmptyContext;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakePerlinRecipe(true), EmptyContext);
	TestTrue(TEXT("Perlin context recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("Context pipeline preserves height scale"), Result.HeightScale, 6400.0f);
	TestTrue(TEXT("Context output has Elevation"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Elevation));
	TestTrue(TEXT("Context output has SlopeDegrees"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::SlopeDegrees));
	TestTrue(TEXT("Context output has Concavity"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Concavity));
	TestTrue(TEXT("Context output has Convexity"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Convexity));
	TestTrue(TEXT("Context output has Mountain"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Mountain));
	TestTrue(TEXT("Context output has Foothill"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Foothill));
	TestTrue(TEXT("Context output has Plains"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Plains));
	TestTrue(TEXT("Context fields survive hydraulic erosion"), Result.Dataset.HasScalarField(EonformTerrainFieldNames::Wear));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainEvaluatorCycleTest,
	"Eonform.Core.Graph.CycleDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainEvaluatorCycleTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe = MakeRecipe();
	FEonformTerrainConnection BackEdge;
	BackEdge.FromNode = Recipe.OutputNode;
	BackEdge.FromOutput = TEXT("Terrain");
	BackEdge.ToNode = FGuid(1, 2, 3, 4);
	BackEdge.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(BackEdge);

	FEonformTerrainEvaluationContext Context;
	Context.SourceDataset = MakeSourceDataset();
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestFalse(TEXT("Cyclic recipe fails"), Result.bSuccess);
	TestTrue(TEXT("Cycle error is reported"), Result.Error.Contains(TEXT("cycle"), ESearchCase::IgnoreCase));
	return true;
}

#endif
