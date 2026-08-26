#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainNode MakePerlinNode()
	{
		FEonformTerrainNode Node;
		Node.Id = FGuid(101, 102, 103, 104);
		Node.Type = EonformTerrainNodeTypes::PerlinNoise;
		Node.IntegerParameters.Add(TEXT("Resolution"), 33);
		Node.IntegerParameters.Add(TEXT("Seed"), 77);
		Node.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
		Node.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Node.NumericParameters.Add(TEXT("Frequency"), 0.0012);
		return Node;
	}

	FEonformTerrainNode MakeShapeNode()
	{
		FEonformTerrainNode Node;
		Node.Id = FGuid(105, 106, 107, 108);
		Node.Type = EonformTerrainNodeTypes::TerrainShape;
		Node.IntegerParameters.Add(TEXT("Seed"), 77);
		Node.NumericParameters.Add(TEXT("MacroStrength"), 0.8);
		Node.NumericParameters.Add(TEXT("WarpStrength"), 600.0);
		Node.NumericParameters.Add(TEXT("RidgeStrength"), 0.6);
		Node.NumericParameters.Add(TEXT("ValleyDepth"), 0.18);
		return Node;
	}

	FEonformTerrainRecipe MakeShapeRecipe(bool bAddContext)
	{
		FEonformTerrainRecipe Recipe;
		const FEonformTerrainNode Source = MakePerlinNode();
		const FEonformTerrainNode Shape = MakeShapeNode();
		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Shape);

		FEonformTerrainConnection SourceToShape;
		SourceToShape.FromNode = Source.Id;
		SourceToShape.FromOutput = TEXT("Terrain");
		SourceToShape.ToNode = Shape.Id;
		SourceToShape.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(SourceToShape);
		Recipe.OutputNode = Shape.Id;

		if (bAddContext)
		{
			FEonformTerrainNode Context;
			Context.Id = FGuid(109, 110, 111, 112);
			Context.Type = EonformTerrainNodeTypes::TerrainContext;
			Recipe.Nodes.Add(Context);

			FEonformTerrainConnection ShapeToContext;
			ShapeToContext.FromNode = Shape.Id;
			ShapeToContext.FromOutput = TEXT("Terrain");
			ShapeToContext.ToNode = Context.Id;
			ShapeToContext.ToInput = TEXT("Terrain");
			Recipe.Connections.Add(ShapeToContext);
			Recipe.OutputNode = Context.Id;
		}

		return Recipe;
	}

	FEonformTerrainRecipe MakeSourceOnlyRecipe()
	{
		FEonformTerrainRecipe Recipe;
		const FEonformTerrainNode Source = MakePerlinNode();
		Recipe.Nodes.Add(Source);
		Recipe.OutputNode = Source.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainShapeGraphTest,
	"Eonform.Core.Graph.PerlinNoiseShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainShapeGraphTest::RunTest(const FString& Parameters)
{
	FEonformTerrainEvaluationContext EmptyContext;
	const FEonformTerrainEvaluationResult SourceResult = FEonformTerrainEvaluator::Evaluate(MakeSourceOnlyRecipe(), EmptyContext);
	const FEonformTerrainEvaluationResult ShapeResult = FEonformTerrainEvaluator::Evaluate(MakeShapeRecipe(false), EmptyContext);

	TestTrue(TEXT("Perlin source evaluates"), SourceResult.bSuccess);
	TestTrue(TEXT("Terrain Shape evaluates"), ShapeResult.bSuccess);
	if (!SourceResult.bSuccess || !ShapeResult.bSuccess)
	{
		if (!SourceResult.Error.IsEmpty()) AddError(SourceResult.Error);
		if (!ShapeResult.Error.IsEmpty()) AddError(ShapeResult.Error);
		return false;
	}

	const FEonformScalarField* SourceHeight = SourceResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* ShapedHeight = ShapeResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Source has Height"), SourceHeight);
	TestNotNull(TEXT("Shape has Height"), ShapedHeight);
	TestTrue(TEXT("Shape publishes Mountain"), ShapeResult.Dataset.HasScalarField(EonformTerrainFieldNames::Mountain));
	TestTrue(TEXT("Shape publishes Foothill"), ShapeResult.Dataset.HasScalarField(EonformTerrainFieldNames::Foothill));
	TestTrue(TEXT("Shape publishes Plains"), ShapeResult.Dataset.HasScalarField(EonformTerrainFieldNames::Plains));
	if (!SourceHeight || !ShapedHeight) return false;

	bool bHeightChanged = false;
	for (int32 Index = 0; Index < SourceHeight->Values.Num() && Index < ShapedHeight->Values.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(SourceHeight->Values[Index], ShapedHeight->Values[Index], KINDA_SMALL_NUMBER))
		{
			bHeightChanged = true;
			break;
		}
	}
	TestTrue(TEXT("Terrain Shape materially changes Height"), bHeightChanged);

	const FEonformTerrainEvaluationResult ContextResult = FEonformTerrainEvaluator::Evaluate(MakeShapeRecipe(true), EmptyContext);
	TestTrue(TEXT("Shape followed by context evaluates"), ContextResult.bSuccess);
	if (!ContextResult.bSuccess)
	{
		AddError(ContextResult.Error);
		return false;
	}

	const FEonformScalarField* ShapeMountain = ShapeResult.Dataset.FindScalarField(EonformTerrainFieldNames::Mountain);
	const FEonformScalarField* ContextMountain = ContextResult.Dataset.FindScalarField(EonformTerrainFieldNames::Mountain);
	TestNotNull(TEXT("Shaped Mountain exists"), ShapeMountain);
	TestNotNull(TEXT("Context Mountain exists"), ContextMountain);
	if (ShapeMountain && ContextMountain)
	{
		TestEqual(TEXT("Terrain Context preserves shaped Mountain mask"), ContextMountain->Values, ShapeMountain->Values);
	}

	return true;
}

#endif
