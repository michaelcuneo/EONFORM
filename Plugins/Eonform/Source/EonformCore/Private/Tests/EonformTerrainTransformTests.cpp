#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformTransformTests
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
	FEonformTerrainTransformDescriptorTest,
	"Eonform.Core.Graph.TransformDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainTransformDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Transform descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Transform, Descriptor));
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
	FEonformTerrainTransformTerminalTest,
	"Eonform.Core.Graph.TransformTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainTransformTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0x91000001, 0x91000002, 0x91000003, 0x91000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9021);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode Transform;
	Transform.Id = FGuid(0x92000001, 0x92000002, 0x92000003, 0x92000004);
	Transform.Type = EonformTerrainNodeTypes::Transform;
	Transform.BoolParameters.Add(TEXT("Uniform"), true);
	Transform.NumericParameters.Add(TEXT("Scale"), 0.9);
	Transform.NumericParameters.Add(TEXT("Angle"), 15.0);
	Transform.BoolParameters.Add(TEXT("FillEdges"), true);
	Transform.NumericParameters.Add(TEXT("X"), 50.0);
	Transform.NumericParameters.Add(TEXT("Y"), -25.0);

	Recipe.Nodes = { Source, Transform };
	EonformTransformTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Transform.Id, TEXT("Terrain"));
	Recipe.OutputNode = Transform.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Transform terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Transform result has Height"), Height);
	return Height != nullptr;
}

#endif
