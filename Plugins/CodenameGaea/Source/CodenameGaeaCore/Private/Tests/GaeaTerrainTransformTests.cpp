#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaTransformTests
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
	FGaeaTerrainTransformDescriptorTest,
	"CodenameGaea.Core.Graph.TransformDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainTransformDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Transform descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Transform, Descriptor));
	TestEqual(TEXT("Transform display name"), Descriptor.DisplayName, FString(TEXT("Transform")));
	TestEqual(TEXT("Transform category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Transform input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Transform output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Transform parameter count"), Descriptor.Parameters.Num(), 11);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Transform output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainTransformTerminalTest,
	"CodenameGaea.Core.Graph.TransformTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainTransformTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x91000001, 0x91000002, 0x91000003, 0x91000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9021);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Transform;
	Transform.Id = FGuid(0x92000001, 0x92000002, 0x92000003, 0x92000004);
	Transform.Type = GaeaTerrainNodeTypes::Transform;
	Transform.BoolParameters.Add(TEXT("Uniform"), true);
	Transform.NumericParameters.Add(TEXT("Scale"), 0.9);
	Transform.NumericParameters.Add(TEXT("Angle"), 15.0);
	Transform.BoolParameters.Add(TEXT("FillEdges"), true);
	Transform.NumericParameters.Add(TEXT("X"), 50.0);
	Transform.NumericParameters.Add(TEXT("Y"), -25.0);

	Recipe.Nodes = { Source, Transform };
	GaeaTransformTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Transform.Id, TEXT("Terrain"));
	Recipe.OutputNode = Transform.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Transform terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Transform result has Height"), Height);
	return Height != nullptr;
}

#endif
