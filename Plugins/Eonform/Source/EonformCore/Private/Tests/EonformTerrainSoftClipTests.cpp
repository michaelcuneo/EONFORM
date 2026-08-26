#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformSoftClipTests
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
	FEonformTerrainSoftClipDescriptorTest,
	"Eonform.Core.Graph.SoftClipDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainSoftClipDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("SoftClip descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::SoftClip, Descriptor));
	TestEqual(TEXT("SoftClip display name"), Descriptor.DisplayName, FString(TEXT("SoftClip")));
	TestEqual(TEXT("SoftClip category"), Descriptor.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("SoftClip input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("SoftClip output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("SoftClip parameter count"), Descriptor.Parameters.Num(), 4);
	if (Descriptor.Inputs.Num() == 1)
	{
		TestEqual(TEXT("SoftClip input pin"), Descriptor.Inputs[0].Name, FName(TEXT("Terrain")));
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("SoftClip output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	const FEonformTerrainParameterDescriptor* ClipMode = Descriptor.Parameters.FindByPredicate(
		[](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("ClipMode")); });
	TestNotNull(TEXT("SoftClip ClipMode parameter exists"), ClipMode);
	if (ClipMode)
	{
		TestTrue(TEXT("SoftClip supports Above Threshold"), ClipMode->NameOptions.Contains(TEXT("Above Threshold")));
		TestTrue(TEXT("SoftClip supports Below Threshold"), ClipMode->NameOptions.Contains(TEXT("Below Threshold")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainSoftClipTerminalTest,
	"Eonform.Core.Graph.SoftClipTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainSoftClipTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xD1000001, 0xD1000002, 0xD1000003, 0xD1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 10401);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode SoftClip;
	SoftClip.Id = FGuid(0xD2000001, 0xD2000002, 0xD2000003, 0xD2000004);
	SoftClip.Type = EonformTerrainNodeTypes::SoftClip;
	SoftClip.NameParameters.Add(TEXT("ClipMode"), TEXT("Above Threshold"));
	SoftClip.NumericParameters.Add(TEXT("Threshold"), 0.5);
	SoftClip.NumericParameters.Add(TEXT("Softness"), 0.1);
	SoftClip.NumericParameters.Add(TEXT("Clipping"), 1.0);

	Recipe.Nodes = { Source, SoftClip };
	EonformSoftClipTests::Connect(Recipe, Source.Id, TEXT("Terrain"), SoftClip.Id, TEXT("Terrain"));
	Recipe.OutputNode = SoftClip.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("SoftClip terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("SoftClip result has Height"), Height);
	return Height != nullptr;
}

#endif
