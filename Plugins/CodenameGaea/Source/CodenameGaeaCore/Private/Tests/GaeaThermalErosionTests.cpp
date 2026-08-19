#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaTerrainRecipe MakeThermalRecipe(bool bApplyThermal)
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(0x41000001, 0x41000002, 0x41000003, 0x41000004);
		Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 33);
		Source.IntegerParameters.Add(TEXT("Seed"), 142);
		Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.0018);

		Recipe.Nodes.Add(Source);
		Recipe.OutputNode = Source.Id;

		if (bApplyThermal)
		{
			FGaeaTerrainNode Thermal;
			Thermal.Id = FGuid(0x42000001, 0x42000002, 0x42000003, 0x42000004);
			Thermal.Type = GaeaTerrainNodeTypes::ThermalErosion;
			Thermal.IntegerParameters.Add(TEXT("Iterations"), 8);
			Thermal.NumericParameters.Add(TEXT("TalusAngle"), 18.0);
			Thermal.NumericParameters.Add(TEXT("Strength"), 0.55);
			Recipe.Nodes.Add(Thermal);

			FGaeaTerrainConnection Connection;
			Connection.FromNode = Source.Id;
			Connection.FromOutput = TEXT("Terrain");
			Connection.ToNode = Thermal.Id;
			Connection.ToInput = TEXT("Terrain");
			Recipe.Connections.Add(Connection);
			Recipe.OutputNode = Thermal.Id;
		}

		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaThermalErosionGraphTest,
	"CodenameGaea.Core.Graph.ThermalErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaThermalErosionGraphTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor ThermalDescriptor;
	TestTrue(
		TEXT("Thermal Erosion descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ThermalErosion, ThermalDescriptor));
	TestEqual(TEXT("Thermal Erosion display name"), ThermalDescriptor.DisplayName, FString(TEXT("Thermal Erosion")));
	TestEqual(TEXT("Thermal Erosion has two inputs"), ThermalDescriptor.Inputs.Num(), 2);
	TestEqual(TEXT("Thermal Erosion has one output"), ThermalDescriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Thermal Erosion has three parameters"), ThermalDescriptor.Parameters.Num(), 3);

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Source = FGaeaTerrainEvaluator::Evaluate(MakeThermalRecipe(false), Context);
	const FGaeaTerrainEvaluationResult Thermal = FGaeaTerrainEvaluator::Evaluate(MakeThermalRecipe(true), Context);

	TestTrue(TEXT("Perlin source evaluates"), Source.bSuccess);
	TestTrue(TEXT("Thermal Erosion evaluates"), Thermal.bSuccess);
	if (!Source.bSuccess || !Thermal.bSuccess)
	{
		if (!Source.bSuccess) AddError(Source.Error);
		if (!Thermal.bSuccess) AddError(Thermal.Error);
		return false;
	}

	const FGaeaScalarField* SourceHeight = Source.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* ThermalHeight = Thermal.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Source has Height"), SourceHeight);
	TestNotNull(TEXT("Thermal output has Height"), ThermalHeight);
	if (!SourceHeight || !ThermalHeight) return false;

	TestEqual(TEXT("Thermal Erosion preserves Height domain"), ThermalHeight->Domain, SourceHeight->Domain);
	TestEqual(TEXT("Thermal Erosion preserves sample count"), ThermalHeight->Values.Num(), SourceHeight->Values.Num());

	double Difference = 0.0;
	for (int32 Index = 0; Index < ThermalHeight->Values.Num(); ++Index)
	{
		Difference += FMath::Abs(ThermalHeight->Values[Index] - SourceHeight->Values[Index]);
	}
	TestTrue(TEXT("Thermal Erosion changes the terrain"), Difference > 0.0001);

	return true;
}

#endif
