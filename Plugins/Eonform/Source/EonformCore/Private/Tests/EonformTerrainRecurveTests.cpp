#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformRecurveTests
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
	FEonformTerrainRecurveDescriptorTest,
	"Eonform.Core.Graph.RecurveDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainRecurveDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Recurve descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Recurve, Descriptor));
	TestEqual(TEXT("Recurve display name"), Descriptor.DisplayName, FString(TEXT("Recurve")));
	TestEqual(TEXT("Recurve category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Recurve input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Recurve output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Recurve parameter count"), Descriptor.Parameters.Num(), 4);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("Recurve input pin"), Descriptor.Inputs[0].Name, FName(TEXT("Terrain")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Recurve output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	if (Descriptor.Parameters.Num() == 4)
	{
		TestEqual(TEXT("Recurve Power"), Descriptor.Parameters[0].Name, FName(TEXT("Power")));
		TestEqual(TEXT("Recurve Scale"), Descriptor.Parameters[1].Name, FName(TEXT("Scale")));
		TestEqual(TEXT("Recurve Iterations"), Descriptor.Parameters[2].Name, FName(TEXT("Iterations")));
		TestEqual(TEXT("Recurve Style"), Descriptor.Parameters[3].Name, FName(TEXT("Style")));
		TestEqual(TEXT("Recurve Style option count"), Descriptor.Parameters[3].NameOptions.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainRecurveTerminalTest,
	"Eonform.Core.Graph.RecurveTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainRecurveTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xB1000001, 0xB1000002, 0xB1000003, 0xB1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9351);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode Recurve;
	Recurve.Id = FGuid(0xB2000001, 0xB2000002, 0xB2000003, 0xB2000004);
	Recurve.Type = EonformTerrainNodeTypes::Recurve;
	Recurve.NumericParameters.Add(TEXT("Power"), 0.5);
	Recurve.NumericParameters.Add(TEXT("Scale"), 0.5);
	Recurve.IntegerParameters.Add(TEXT("Iterations"), 2);
	Recurve.NameParameters.Add(TEXT("Style"), TEXT("Inward"));

	Recipe.Nodes = { Source, Recurve };
	EonformRecurveTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Recurve.Id, TEXT("Terrain"));
	Recipe.OutputNode = Recurve.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Recurve terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Recurve result has Height"), Height);
	return Height != nullptr;
}

#endif
