#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace GaeaZeroBordersTests
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
	FGaeaTerrainZeroBordersDescriptorTest,
	"CodenameGaea.Core.Graph.EdgeDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainZeroBordersDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Edge descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ZeroBorders, Descriptor));
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
	FGaeaTerrainZeroBordersTerminalTest,
	"CodenameGaea.Core.Graph.EdgeTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainZeroBordersTerminalTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 9001);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 3200.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0017);

	FGaeaTerrainNode Edge;
	Edge.Id = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
	Edge.Type = GaeaTerrainNodeTypes::ZeroBorders;
	Edge.NameParameters.Add(TEXT("Style"), TEXT("Square"));
	Edge.NumericParameters.Add(TEXT("Size"), 0.2);
	Edge.IntegerParameters.Add(TEXT("Pixels"), 0);
	Edge.NumericParameters.Add(TEXT("Softness"), 0.5);

	Recipe.Nodes = { Source, Edge };
	GaeaZeroBordersTests::Connect(Recipe, Source.Id, TEXT("Terrain"), Edge.Id, TEXT("Terrain"));
	Recipe.OutputNode = Edge.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Edge terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Edge result has Height"), Height);
	if (!Height) return false;

	const int32 LastX = Height->Domain.Dimensions.X - 1;
	const int32 LastY = Height->Domain.Dimensions.Y - 1;
	TestEqual(TEXT("Top-left border is zero"), Height->AtInterior(0, 0), 0.0f);
	TestEqual(TEXT("Bottom-right border is zero"), Height->AtInterior(LastX, LastY), 0.0f);
	return true;
}

#endif
