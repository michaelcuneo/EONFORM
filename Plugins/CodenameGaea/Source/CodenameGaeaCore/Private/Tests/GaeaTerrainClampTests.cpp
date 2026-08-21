#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainClampDescriptorTest,
	"CodenameGaea.Core.Graph.ClampDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainClampDescriptorTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Clamp;
	TestTrue(TEXT("Clamp descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Clamp, Clamp));
	TestEqual(TEXT("Clamp display name"), Clamp.DisplayName, FString(TEXT("Clamp")));
	TestEqual(TEXT("Clamp category"), Clamp.Category, FString(TEXT("Adjustments")));
	TestEqual(TEXT("Clamp has one input"), Clamp.Inputs.Num(), 1);
	TestEqual(TEXT("Clamp has one output"), Clamp.Outputs.Num(), 1);
	TestEqual(TEXT("Clamp has four parameters"), Clamp.Parameters.Num(), 4);
	if (Clamp.Inputs.Num() == 1)
	{
		TestEqual(TEXT("Clamp input name"), Clamp.Inputs[0].Name, FName(TEXT("Terrain")));
		TestEqual(TEXT("Clamp input display"), Clamp.Inputs[0].DisplayName, FString(TEXT("Input")));
	}
	if (Clamp.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Clamp output name"), Clamp.Outputs[0].Name, FName(TEXT("Out")));
		TestEqual(TEXT("Clamp output display"), Clamp.Outputs[0].DisplayName, FString(TEXT("Out")));
	}

	const FGaeaTerrainParameterDescriptor* Operation = Clamp.Parameters.FindByPredicate(
		[](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("Operation"));
		});
	TestNotNull(TEXT("Clamp Operation parameter exists"), Operation);
	if (Operation)
	{
		TestEqual(TEXT("Clamp Operation default"), Operation->DefaultName, FName(TEXT("Clamp")));
		TestEqual(TEXT("Clamp Operation option count"), Operation->NameOptions.Num(), 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainClampTerminalOutputTest,
	"CodenameGaea.Core.Graph.ClampTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainClampTerminalOutputTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(1001, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 10101);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Clamp;
	Clamp.Id = FGuid(1002, 2, 2, 2);
	Clamp.Type = GaeaTerrainNodeTypes::Clamp;
	Clamp.NumericParameters.Add(TEXT("Min"), 0.2);
	Clamp.NumericParameters.Add(TEXT("Max"), 0.8);
	Clamp.NameParameters.Add(TEXT("Operation"), TEXT("Clip"));
	Clamp.BoolParameters.Add(TEXT("DropToFloor"), false);

	Recipe.Nodes = { Source, Clamp };

	FGaeaTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Clamp.Id;
	Connection.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = Clamp.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Clamp can terminate a terrain graph through Out"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Clamp terminal result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Clamp terminal Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
