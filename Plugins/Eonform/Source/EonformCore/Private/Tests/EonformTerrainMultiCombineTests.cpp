#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformMultiCombineTests
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
	FEonformTerrainMultiCombineDescriptorTest,
	"Eonform.Core.Graph.MultiCombineDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainMultiCombineDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("MultiCombine descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::MultiCombine, Descriptor));
	TestEqual(TEXT("MultiCombine has ten inputs"), Descriptor.Inputs.Num(), 10);
	TestEqual(TEXT("MultiCombine output is Out"), Descriptor.Outputs.Num() > 0 ? Descriptor.Outputs[0].Name : NAME_None, FName(TEXT("Out")));
	TestEqual(TEXT("MultiCombine has Method, Uniform, Ratio and nine layer ratios"), Descriptor.Parameters.Num(), 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainMultiCombineTerminalTest,
	"Eonform.Core.Graph.MultiCombineTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainMultiCombineTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode SourceA;
	SourceA.Id = FGuid(0x81000001, 0x81000002, 0x81000003, 0x81000004);
	SourceA.Type = EonformTerrainNodeTypes::PerlinNoise;
	SourceA.IntegerParameters.Add(TEXT("Resolution"), 17);
	SourceA.IntegerParameters.Add(TEXT("Seed"), 1201);
	SourceA.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	SourceA.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	SourceA.NumericParameters.Add(TEXT("Frequency"), 0.0014);

	FEonformTerrainNode SourceB = SourceA;
	SourceB.Id = FGuid(0x82000001, 0x82000002, 0x82000003, 0x82000004);
	SourceB.IntegerParameters[TEXT("Seed")] = 2202;

	FEonformTerrainNode Multi;
	Multi.Id = FGuid(0x83000001, 0x83000002, 0x83000003, 0x83000004);
	Multi.Type = EonformTerrainNodeTypes::MultiCombine;
	Multi.NameParameters.Add(TEXT("Method"), TEXT("Blend"));
	Multi.BoolParameters.Add(TEXT("Uniform"), true);
	Multi.NumericParameters.Add(TEXT("Ratio"), 0.5);

	Recipe.Nodes = { SourceA, SourceB, Multi };
	EonformMultiCombineTests::Connect(Recipe, SourceA.Id, TEXT("Terrain"), Multi.Id, TEXT("Input"));
	EonformMultiCombineTests::Connect(Recipe, SourceB.Id, TEXT("Terrain"), Multi.Id, TEXT("Layer1"));
	Recipe.OutputNode = Multi.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("MultiCombine terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("MultiCombine result has Height"), Height);
	return Height != nullptr;
}

#endif
