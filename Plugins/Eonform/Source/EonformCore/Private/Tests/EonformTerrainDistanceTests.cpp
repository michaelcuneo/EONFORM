#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformDistanceTests
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
	FEonformTerrainDistanceDescriptorTest,
	"Eonform.Core.Graph.DistanceDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainDistanceDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Distance descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Distance, Descriptor));
	TestEqual(TEXT("Distance display name"), Descriptor.DisplayName, FString(TEXT("Distance")));
	TestEqual(TEXT("Distance category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Distance input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Distance output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Distance parameter count"), Descriptor.Parameters.Num(), 13);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Distance output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainDistanceTerminalTest,
	"Eonform.Core.Graph.DistanceTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainDistanceTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xB1000001, 0xB1000002, 0xB1000003, 0xB1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 7001);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode Distance;
	Distance.Id = FGuid(0xB2000001, 0xB2000002, 0xB2000003, 0xB2000004);
	Distance.Type = EonformTerrainNodeTypes::Distance;
	Distance.NumericParameters.Add(TEXT("Falloff"), 0.25);
	Distance.NumericParameters.Add(TEXT("Threshold"), 0.5);

	Recipe.Nodes = { Source, Distance };
	EonformDistanceTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Distance.Id, TEXT("Input"));
	Recipe.OutputNode = Distance.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Distance terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Distance result has Height"), Height);
	return Height != nullptr;
}

#endif
