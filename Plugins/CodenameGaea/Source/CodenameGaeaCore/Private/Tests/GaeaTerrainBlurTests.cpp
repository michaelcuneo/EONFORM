#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaTerrainBlurTest, "CodenameGaea.Core.Graph.Blur", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainBlurTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Blur descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Blur, Descriptor));
	TestEqual(TEXT("Blur output is Out"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	TestEqual(TEXT("Blur has five parameters"), Descriptor.Parameters.Num(), 5);

	FGaeaTerrainRecipe Recipe;
	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 33);
	Source.IntegerParameters.Add(TEXT("Seed"), 7171);
	Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);

	FGaeaTerrainNode Blur;
	Blur.Id = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
	Blur.Type = GaeaTerrainNodeTypes::Blur;
	Blur.NumericParameters.Add(TEXT("Power"), 0.75);
	Blur.IntegerParameters.Add(TEXT("Iterations"), 2);
	Blur.NameParameters.Add(TEXT("Type"), TEXT("Gaussian"));

	Recipe.Nodes = { Source, Blur };
	FGaeaTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Blur.Id;
	Connection.ToInput = TEXT("Input");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = Blur.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Blur terrain recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	TestNotNull(TEXT("Blur output has Height"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height));
	return Result.bSuccess;
}

#endif
