#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaSoftClipTests
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
	FGaeaTerrainSoftClipDescriptorTest,
	"CodenameGaea.Core.Graph.SoftClipDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainSoftClipDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("SoftClip descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::SoftClip, Descriptor));
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
	const FGaeaTerrainParameterDescriptor* ClipMode = Descriptor.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("ClipMode")); });
	TestNotNull(TEXT("SoftClip ClipMode parameter exists"), ClipMode);
	if (ClipMode)
	{
		TestTrue(TEXT("SoftClip supports Above Threshold"), ClipMode->NameOptions.Contains(TEXT("Above Threshold")));
		TestTrue(TEXT("SoftClip supports Below Threshold"), ClipMode->NameOptions.Contains(TEXT("Below Threshold")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainSoftClipTerminalTest,
	"CodenameGaea.Core.Graph.SoftClipTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainSoftClipTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0xD1000001, 0xD1000002, 0xD1000003, 0xD1000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 10401);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode SoftClip;
	SoftClip.Id = FGuid(0xD2000001, 0xD2000002, 0xD2000003, 0xD2000004);
	SoftClip.Type = GaeaTerrainNodeTypes::SoftClip;
	SoftClip.NameParameters.Add(TEXT("ClipMode"), TEXT("Above Threshold"));
	SoftClip.NumericParameters.Add(TEXT("Threshold"), 0.5);
	SoftClip.NumericParameters.Add(TEXT("Softness"), 0.1);
	SoftClip.NumericParameters.Add(TEXT("Clipping"), 1.0);

	Recipe.Nodes = { Source, SoftClip };
	GaeaSoftClipTests::Connect(Recipe, Source.Id, TEXT("Terrain"), SoftClip.Id, TEXT("Terrain"));
	Recipe.OutputNode = SoftClip.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("SoftClip terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("SoftClip result has Height"), Height);
	return Height != nullptr;
}

#endif
