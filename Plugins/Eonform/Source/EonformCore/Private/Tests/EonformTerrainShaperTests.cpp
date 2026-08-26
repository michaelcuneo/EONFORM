#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformShaperTests
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
	FEonformTerrainShaperDescriptorTest,
	"Eonform.Core.Graph.ShaperDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainShaperDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Shaper descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Shaper, Descriptor));
	TestEqual(TEXT("Shaper display name"), Descriptor.DisplayName, FString(TEXT("Shaper")));
	TestEqual(TEXT("Shaper category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Shaper input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Shaper output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Shaper parameter count"), Descriptor.Parameters.Num(), 5);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("Shaper input pin"), Descriptor.Inputs[0].Name, FName(TEXT("Terrain")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Shaper output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainShaperTerminalTest,
	"Eonform.Core.Graph.ShaperTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainShaperTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xC1000001, 0xC1000002, 0xC1000003, 0xC1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 6011);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode Shaper;
	Shaper.Id = FGuid(0xC2000001, 0xC2000002, 0xC2000003, 0xC2000004);
	Shaper.Type = EonformTerrainNodeTypes::Shaper;
	Shaper.NumericParameters.Add(TEXT("Shape"), 0.35);
	Shaper.NumericParameters.Add(TEXT("LocalEffect"), 0.25);
	Shaper.NumericParameters.Add(TEXT("LocalArea"), 0.5);
	Shaper.BoolParameters.Add(TEXT("MaintainFineDetails"), true);
	Shaper.NumericParameters.Add(TEXT("DetailSize"), 0.25);

	Recipe.Nodes = { Source, Shaper };
	EonformShaperTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Shaper.Id, TEXT("Terrain"));
	Recipe.OutputNode = Shaper.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Shaper terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Shaper result has Height"), Height);
	return Height != nullptr;
}

#endif
