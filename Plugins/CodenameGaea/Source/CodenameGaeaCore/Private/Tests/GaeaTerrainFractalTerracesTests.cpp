#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaFractalTerracesTests
{
	void Connect(FGaeaTerrainRecipe& Recipe, const FGuid& FromNode, FName FromOutput, const FGuid& ToNode, FName ToInput)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = FromNode;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = ToNode;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainFractalTerracesDescriptorTest,
	"CodenameGaea.Core.Graph.FractalTerracesDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainFractalTerracesDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("FractalTerraces descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::FractalTerraces, Descriptor));
	TestEqual(TEXT("FractalTerraces display name"), Descriptor.DisplayName, FString(TEXT("FractalTerraces")));
	TestEqual(TEXT("FractalTerraces category"), Descriptor.Category, FString(TEXT("Profile")));
	TestEqual(TEXT("FractalTerraces input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("FractalTerraces output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("FractalTerraces parameter count"), Descriptor.Parameters.Num(), 13);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("FractalTerraces input pin"), Descriptor.Inputs[0].Name, FName(TEXT("Terrain")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("FractalTerraces output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainFractalTerracesTerminalTest,
	"CodenameGaea.Core.Graph.FractalTerracesTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainFractalTerracesTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9127);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0019);

	FGaeaTerrainNode Terraces;
	Terraces.Id = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
	Terraces.Type = GaeaTerrainNodeTypes::FractalTerraces;
	Terraces.NameParameters.Add(TEXT("Mode"), TEXT("Improved"));
	Terraces.NumericParameters.Add(TEXT("Spacing"), 0.15);
	Terraces.IntegerParameters.Add(TEXT("Octaves"), 4);
	Terraces.NumericParameters.Add(TEXT("Intensity"), 0.65);
	Terraces.NumericParameters.Add(TEXT("Shape"), 0.5);
	Terraces.IntegerParameters.Add(TEXT("Seed"), 1337);

	Recipe.Nodes = { Source, Terraces };
	GaeaFractalTerracesTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Terraces.Id, TEXT("Terrain"));
	Recipe.OutputNode = Terraces.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("FractalTerraces terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("FractalTerraces result has Height"), Height);
	return Height != nullptr;
}

#endif
