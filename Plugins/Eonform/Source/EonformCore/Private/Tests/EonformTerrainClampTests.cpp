#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainClampDescriptorTest,
	"Eonform.Core.Graph.ClampDescriptor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainClampDescriptorTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Clamp;
	TestTrue(TEXT("Clamp descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Clamp, Clamp));
	TestEqual(TEXT("Clamp display name"), Clamp.DisplayName, FString(TEXT("Clamp")));
	TestEqual(TEXT("Clamp category"), Clamp.Category, FString(TEXT("Modify")));
	TestEqual(TEXT("Clamp has one input"), Clamp.Inputs.Num(), 1);
	TestEqual(TEXT("Clamp has one output"), Clamp.Outputs.Num(), 1);
	TestEqual(TEXT("Clamp has three parameters"), Clamp.Parameters.Num(), 3);
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

	const FEonformTerrainParameterDescriptor* Mode = Clamp.Parameters.FindByPredicate(
		[](const FEonformTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == FName(TEXT("Mode"));
		});
	TestNotNull(TEXT("Clamp Mode parameter exists"), Mode);
	if (Mode)
	{
		TestEqual(TEXT("Clamp Mode default"), Mode->DefaultName, FName(TEXT("Standard")));
		TestEqual(TEXT("Clamp Mode option count"), Mode->NameOptions.Num(), 2);
	}
	TestNotNull(TEXT("Clamp Value range exists"), Clamp.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Value")); }));
	TestNotNull(TEXT("Clamp Drop exists"), Clamp.Parameters.FindByPredicate([](const FEonformTerrainParameterDescriptor& P) { return P.Name == FName(TEXT("Drop")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainClampTerminalOutputTest,
	"Eonform.Core.Graph.ClampTerminalOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainClampTerminalOutputTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(1001, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 10101);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FEonformTerrainNode Clamp;
	Clamp.Id = FGuid(1002, 2, 2, 2);
	Clamp.Type = EonformTerrainNodeTypes::Clamp;
	Clamp.NumericParameters.Add(TEXT("ValueMin"), 0.2);
	Clamp.NumericParameters.Add(TEXT("ValueMax"), 0.8);
	Clamp.NameParameters.Add(TEXT("Mode"), TEXT("Normalized"));
	Clamp.BoolParameters.Add(TEXT("Drop"), false);

	Recipe.Nodes = { Source, Clamp };

	FEonformTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Clamp.Id;
	Connection.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(Connection);
	Recipe.OutputNode = Clamp.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Clamp can terminate a terrain graph through Out"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Clamp terminal result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Clamp terminal Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
