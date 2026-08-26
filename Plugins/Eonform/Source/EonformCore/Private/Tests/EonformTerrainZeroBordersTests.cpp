#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace EonformZeroBordersTests
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
	FEonformTerrainZeroBordersDescriptorTest,
	"Eonform.Core.Graph.EdgeDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainZeroBordersDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Edge descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::ZeroBorders, Descriptor));
	TestEqual(TEXT("Edge display name"), Descriptor.DisplayName, FString(TEXT("Edge")));
	TestEqual(TEXT("Edge category"), Descriptor.Category, FString(TEXT("Utility")));
	TestEqual(TEXT("Edge input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Edge output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Edge parameter count"), Descriptor.Parameters.Num(), 4);
	if (Descriptor.Parameters.Num() == 4)
	{
		TestEqual(TEXT("Edge Style"), Descriptor.Parameters[0].Name, FName(TEXT("Style")));
		TestEqual(TEXT("Edge Size"), Descriptor.Parameters[1].Name, FName(TEXT("Size")));
		TestEqual(TEXT("Edge Pixels"), Descriptor.Parameters[2].Name, FName(TEXT("Pixels")));
		TestEqual(TEXT("Edge Softness"), Descriptor.Parameters[3].Name, FName(TEXT("Softness")));
		TestEqual(TEXT("Edge Style option count"), Descriptor.Parameters[0].NameOptions.Num(), 3);
	}
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Edge output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainZeroBordersTerminalTest,
	"Eonform.Core.Graph.EdgeTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainZeroBordersTerminalTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9001);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FEonformTerrainNode Edge;
	Edge.Id = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
	Edge.Type = EonformTerrainNodeTypes::ZeroBorders;
	Edge.NameParameters.Add(TEXT("Style"), TEXT("Square"));
	Edge.NumericParameters.Add(TEXT("Size"), 0.2);
	Edge.IntegerParameters.Add(TEXT("Pixels"), 0);
	Edge.NumericParameters.Add(TEXT("Softness"), 0.5);

	Recipe.Nodes = { Source, Edge };
	EonformZeroBordersTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Edge.Id, TEXT("Terrain"));
	Recipe.OutputNode = Edge.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Edge terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Edge result has Height"), Height);
	if (!Height) return false;

	const int32 LastX = Height->Domain.Dimensions.X - 1;
	const int32 LastY = Height->Domain.Dimensions.Y - 1;
	TestEqual(TEXT("Top-left border is zero"), Height->AtInterior(0, 0), 0.0f);
	TestEqual(TEXT("Bottom-right border is zero"), Height->AtInterior(LastX, LastY), 0.0f);
	return true;
}

#endif
