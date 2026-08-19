#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaTerrainNode MakeProceduralNode()
	{
		FGaeaTerrainNode Node;
		Node.Id = FGuid(101, 102, 103, 104);
		Node.Type = GaeaTerrainNodeTypes::ProceduralTerrain;
		Node.IntegerParameters.Add(TEXT("Resolution"), 33);
		Node.IntegerParameters.Add(TEXT("Seed"), 77);
		Node.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
		Node.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Node.NumericParameters.Add(TEXT("Frequency"), 0.0012);
		return Node;
	}

	FGaeaTerrainNode MakeShapeNode()
	{
		FGaeaTerrainNode Node;
		Node.Id = FGuid(105, 106, 107, 108);
		Node.Type = GaeaTerrainNodeTypes::TerrainShape;
		Node.IntegerParameters.Add(TEXT("Seed"), 77);
		Node.NumericParameters.Add(TEXT("MacroStrength"), 0.8);
		Node.NumericParameters.Add(TEXT("WarpStrength"), 600.0);
		Node.NumericParameters.Add(TEXT("RidgeStrength"), 0.6);
		Node.NumericParameters.Add(TEXT("ValleyDepth"), 0.18);
		return Node;
	}

	FGaeaTerrainRecipe MakeShapeRecipe(bool bAddContext)
	{
		FGaeaTerrainRecipe Recipe;
		const FGaeaTerrainNode Source = MakeProceduralNode();
		const FGaeaTerrainNode Shape = MakeShapeNode();
		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Shape);

		FGaeaTerrainConnection SourceToShape;
		SourceToShape.FromNode = Source.Id;
		SourceToShape.FromOutput = TEXT("Terrain");
		SourceToShape.ToNode = Shape.Id;
		SourceToShape.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(SourceToShape);
		Recipe.OutputNode = Shape.Id;

		if (bAddContext)
		{
			FGaeaTerrainNode Context;
			Context.Id = FGuid(109, 110, 111, 112);
			Context.Type = GaeaTerrainNodeTypes::TerrainContext;
			Recipe.Nodes.Add(Context);

			FGaeaTerrainConnection ShapeToContext;
			ShapeToContext.FromNode = Shape.Id;
			ShapeToContext.FromOutput = TEXT("Terrain");
			ShapeToContext.ToNode = Context.Id;
			ShapeToContext.ToInput = TEXT("Terrain");
			Recipe.Connections.Add(ShapeToContext);
			Recipe.OutputNode = Context.Id;
		}

		return Recipe;
	}

	FGaeaTerrainRecipe MakeSourceOnlyRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		const FGaeaTerrainNode Source = MakeProceduralNode();
		Recipe.Nodes.Add(Source);
		Recipe.OutputNode = Source.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainShapeGraphTest,
	"CodenameGaea.Core.Graph.ProceduralTerrainShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainShapeGraphTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainEvaluationResult SourceResult = FGaeaTerrainEvaluator::Evaluate(MakeSourceOnlyRecipe(), EmptyContext);
	const FGaeaTerrainEvaluationResult ShapeResult = FGaeaTerrainEvaluator::Evaluate(MakeShapeRecipe(false), EmptyContext);

	TestTrue(TEXT("Procedural source evaluates"), SourceResult.bSuccess);
	TestTrue(TEXT("Terrain Shape evaluates"), ShapeResult.bSuccess);
	if (!SourceResult.bSuccess || !ShapeResult.bSuccess)
	{
		if (!SourceResult.Error.IsEmpty()) AddError(SourceResult.Error);
		if (!ShapeResult.Error.IsEmpty()) AddError(ShapeResult.Error);
		return false;
	}

	const FGaeaScalarField* SourceHeight = SourceResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* ShapedHeight = ShapeResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Source has Height"), SourceHeight);
	TestNotNull(TEXT("Shape has Height"), ShapedHeight);
	TestTrue(TEXT("Shape publishes Mountain"), ShapeResult.Dataset.HasScalarField(GaeaTerrainFieldNames::Mountain));
	TestTrue(TEXT("Shape publishes Foothill"), ShapeResult.Dataset.HasScalarField(GaeaTerrainFieldNames::Foothill));
	TestTrue(TEXT("Shape publishes Plains"), ShapeResult.Dataset.HasScalarField(GaeaTerrainFieldNames::Plains));
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

	const FGaeaTerrainEvaluationResult ContextResult = FGaeaTerrainEvaluator::Evaluate(MakeShapeRecipe(true), EmptyContext);
	TestTrue(TEXT("Shape followed by context evaluates"), ContextResult.bSuccess);
	if (!ContextResult.bSuccess)
	{
		AddError(ContextResult.Error);
		return false;
	}

	const FGaeaScalarField* ShapeMountain = ShapeResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
	const FGaeaScalarField* ContextMountain = ContextResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
	TestNotNull(TEXT("Shaped Mountain exists"), ShapeMountain);
	TestNotNull(TEXT("Context Mountain exists"), ContextMountain);
	if (ShapeMountain && ContextMountain)
	{
		TestEqual(TEXT("Terrain Context preserves shaped Mountain mask"), ContextMountain->Values, ShapeMountain->Values);
	}

	return true;
}

#endif
