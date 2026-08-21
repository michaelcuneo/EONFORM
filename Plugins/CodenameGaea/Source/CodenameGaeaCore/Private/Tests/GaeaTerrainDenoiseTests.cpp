#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainDenoiseTerminalTest,
	"CodenameGaea.Core.Graph.DenoiseTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainDenoiseTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Denoise descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Denoise, Descriptor));
	TestEqual(TEXT("Denoise display name"), Descriptor.DisplayName, FString(TEXT("Denoise")));
	TestEqual(TEXT("Denoise category"), Descriptor.Category, FString(TEXT("Adjustments")));

	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 33);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);
	Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Denoise;
	Denoise.Id = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
	Denoise.Type = GaeaTerrainNodeTypes::Denoise;
	Denoise.NumericParameters.Add(TEXT("Strength"), 0.75);
	Denoise.NumericParameters.Add(TEXT("Despeckle"), 0.5);
	Denoise.BoolParameters.Add(TEXT("ApplyEnhancement"), true);

	Recipe.Nodes = { Source, Denoise };

	FGaeaTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Denoise.Id;
	Connection.ToInput = TEXT("Input");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = Denoise.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Perlin to Denoise.Out evaluates as terminal terrain"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	return Result.bSuccess;
}

#endif
