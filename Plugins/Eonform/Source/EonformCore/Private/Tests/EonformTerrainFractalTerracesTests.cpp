#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformFractalTerracesTests
{
	void Connect(FEonformTerrainRecipe& Recipe, const FGuid& FromNode, FName FromOutput, const FGuid& ToNode, FName ToInput)
	{
		FEonformTerrainConnection Connection;
		Connection.FromNode = FromNode;
		Connection.FromOutput = FromOutput;
		Connection.ToNode = ToNode;
		Connection.ToInput = ToInput;
		Recipe.Connections.Add(Connection);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainFractalTerracesDescriptorTest,
	"Eonform.Core.Graph.FractalTerracesDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainFractalTerracesDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("FractalTerraces descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::FractalTerraces, Descriptor));
	TestEqual(TEXT("FractalTerraces display name"), Descriptor.DisplayName, FString(TEXT("FractalTerraces")));
	TestEqual(TEXT("FractalTerraces category"), Descriptor.Category, FString(TEXT("Surface")));
	TestEqual(TEXT("FractalTerraces input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("FractalTerraces output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("FractalTerraces parameter count"), Descriptor.Parameters.Num(), 20);
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
	FEonformTerrainFractalTerracesTerminalTest,
	"Eonform.Core.Graph.FractalTerracesTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainFractalTerracesTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9127);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0019);

	FEonformTerrainNode Terraces;
	Terraces.Id = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
	Terraces.Type = EonformTerrainNodeTypes::FractalTerraces;
	Terraces.NameParameters.Add(TEXT("Mode"), TEXT("Improved"));
	Terraces.NumericParameters.Add(TEXT("Spacing"), 0.15);
	Terraces.IntegerParameters.Add(TEXT("Octaves"), 4);
	Terraces.NumericParameters.Add(TEXT("Intensity"), 0.65);
	Terraces.NumericParameters.Add(TEXT("Shape"), 0.5);
	Terraces.IntegerParameters.Add(TEXT("Seed"), 1337);

	Recipe.Nodes = { Source, Terraces };
	EonformFractalTerracesTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Terraces.Id, TEXT("Terrain"));
	Recipe.OutputNode = Terraces.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("FractalTerraces terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("FractalTerraces result has Height"), Height);
	return Height != nullptr;
}

#endif
