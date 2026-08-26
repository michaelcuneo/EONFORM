#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformTerrainBlurTest, "Eonform.Core.Graph.Blur", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainBlurTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Blur descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Blur, Descriptor));
	TestEqual(TEXT("Blur category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Blur output is Out"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	TestEqual(TEXT("Blur has one parameter"), Descriptor.Parameters.Num(), 1);
	if (Descriptor.Parameters.Num() == 1)
	{
		TestEqual(TEXT("Blur parameter is Radius"), Descriptor.Parameters[0].Name, FName(TEXT("Radius")));
	}

	FEonformTerrainRecipe Recipe;
	FEonformTerrainNode Source;
	Source.Id = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 33);
	Source.IntegerParameters.Add(TEXT("Seed"), 7171);
	Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);

	FEonformTerrainNode Blur;
	Blur.Id = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
	Blur.Type = EonformTerrainNodeTypes::Blur;
	Blur.NumericParameters.Add(TEXT("Radius"), 0.35);

	Recipe.Nodes = { Source, Blur };
	FEonformTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Blur.Id;
	Connection.ToInput = TEXT("Input");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = Blur.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Blur terrain recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	TestNotNull(TEXT("Blur output has Height"), Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height));
	return Result.bSuccess;
}

#endif
