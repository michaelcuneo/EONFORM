#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainDenoiseTerminalTest,
	"Eonform.Core.Graph.DenoiseTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainDenoiseTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Denoise descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Denoise, Descriptor));
	TestEqual(TEXT("Denoise display name"), Descriptor.DisplayName, FString(TEXT("Denoise")));
	TestEqual(TEXT("Denoise category"), Descriptor.Category, FString(TEXT("Modify")));

	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 33);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);
	Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FEonformTerrainNode Denoise;
	Denoise.Id = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
	Denoise.Type = EonformTerrainNodeTypes::Denoise;
	Denoise.NumericParameters.Add(TEXT("Strength"), 0.75);
	Denoise.NumericParameters.Add(TEXT("Despeckle"), 0.5);
	Denoise.BoolParameters.Add(TEXT("ApplyEnhancement"), true);

	Recipe.Nodes = { Source, Denoise };

	FEonformTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Denoise.Id;
	Connection.ToInput = TEXT("Input");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = Denoise.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Perlin to Denoise.Out evaluates as terminal terrain"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	return Result.bSuccess;
}

#endif
