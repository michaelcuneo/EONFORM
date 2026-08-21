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
	"CodenameGaea.Core.Graph.ZeroBordersDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainZeroBordersDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Zero Borders descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ZeroBorders, Descriptor));
	TestEqual(TEXT("Zero Borders display name"), Descriptor.DisplayName, FString(TEXT("Zero Borders")));
	TestEqual(TEXT("Zero Borders category"), Descriptor.Category, FString(TEXT("Adjustments")));
	TestEqual(TEXT("Zero Borders input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Zero Borders output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Zero Borders parameter count"), Descriptor.Parameters.Num(), 6);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Zero Borders output pin"), Descriptor.Outputs[0].Name, FName(TEXT("Out")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainZeroBordersTerminalTest,
	"CodenameGaea.Core.Graph.ZeroBordersTerminalOut",
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

	FGaeaTerrainNode ZeroBorders;
	ZeroBorders.Id = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
	ZeroBorders.Type = GaeaTerrainNodeTypes::ZeroBorders;
	ZeroBorders.NameParameters.Add(TEXT("Mode"), TEXT("Square"));
	ZeroBorders.NumericParameters.Add(TEXT("Margin"), 0.1);
	ZeroBorders.NumericParameters.Add(TEXT("Falloff"), 0.2);
	ZeroBorders.BoolParameters.Add(TEXT("Auto"), false);
	ZeroBorders.NumericParameters.Add(TEXT("BlurPower"), 0.0);
	ZeroBorders.IntegerParameters.Add(TEXT("Iterations"), 1);

	Recipe.Nodes = { Source, ZeroBorders };
	GaeaZeroBordersTests::Connect(Recipe, Source.Id, TEXT("Terrain"), ZeroBorders.Id, TEXT("Terrain"));
	Recipe.OutputNode = ZeroBorders.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Zero Borders terminal Out evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Zero Borders result has Height"), Height);
	if (!Height) return false;

	const int32 LastX = Height->Domain.Dimensions.X - 1;
	const int32 LastY = Height->Domain.Dimensions.Y - 1;
	TestEqual(TEXT("Top-left border is zero"), Height->AtInterior(0, 0), 0.0f);
	TestEqual(TEXT("Bottom-right border is zero"), Height->AtInterior(LastX, LastY), 0.0f);
	return true;
}

#endif
