#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaEmptyTerrainGraphTest,
	"CodenameGaea.Core.Graph.EmptyGraphProducesFlatTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaEmptyTerrainGraphTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;
	FString Error;
	TestTrue(TEXT("Empty recipe validates"), Recipe.Validate(&Error));

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Empty graph evaluates successfully"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Empty graph produces Height"), Height);
	if (!Height)
	{
		return false;
	}

	TestEqual(TEXT("Default flat resolution"), Height->Domain.Dimensions, FIntPoint(257, 257));
	TestEqual(TEXT("Default flat sample count"), Height->Values.Num(), 257 * 257);
	for (int32 Index = 0; Index < Height->Values.Num(); ++Index)
	{
		if (!FMath::IsNearlyZero(Height->Values[Index]))
		{
			AddError(FString::Printf(TEXT("Default flat terrain is non-zero at sample %d."), Index));
			break;
		}
	}

	return true;
}

#endif
