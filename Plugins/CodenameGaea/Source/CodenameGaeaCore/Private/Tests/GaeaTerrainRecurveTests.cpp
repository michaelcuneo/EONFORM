#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaRecurveTests
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
	FGaeaTerrainRecurveDescriptorTest,
	"CodenameGaea.Core.Graph.RecurveDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainRecurveDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Recurve descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Recurve, Descriptor));
	TestEqual(TEXT("Recurve display name"), Descriptor.DisplayName, FString(TEXT("Recurve")));
	TestEqual(TEXT("Recurve category"), Descriptor.Category, FString(TEXT("Profile")));
	TestEqual(TEXT("Recurve input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Recurve output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Recurve parameter count"), Descriptor.Parameters.Num(), 7);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("Recurve input pin"), Descriptor.Inputs[0].Name, FName(TEXT("Terrain")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Recurve output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainRecurveTerminalTest,
	"CodenameGaea.Core.Graph.RecurveTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainRecurveTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0xB1000001, 0xB1000002, 0xB1000003, 0xB1000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9351);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Recurve;
	Recurve.Id = FGuid(0xB2000001, 0xB2000002, 0xB2000003, 0xB2000004);
	Recurve.Type = GaeaTerrainNodeTypes::Recurve;
	Recurve.NumericParameters.Add(TEXT("Power"), 0.5);
	Recurve.NumericParameters.Add(TEXT("Scale"), 0.5);
	Recurve.IntegerParameters.Add(TEXT("Duration"), 2);
	Recurve.NumericParameters.Add(TEXT("Degrees"), 45.0);
	Recurve.BoolParameters.Add(TEXT("Inflate"), true);
	Recurve.BoolParameters.Add(TEXT("PreserveFidelity"), false);

	Recipe.Nodes = { Source, Recurve };
	GaeaRecurveTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Recurve.Id, TEXT("Terrain"));
	Recipe.OutputNode = Recurve.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Recurve terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Recurve result has Height"), Height);
	return Height != nullptr;
}

#endif
