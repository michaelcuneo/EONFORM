#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformTerrainRecipe MakeThermalRecipe(bool bApplyThermal)
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(0x41000001, 0x41000002, 0x41000003, 0x41000004);
		Source.Type = EonformTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 33);
		Source.IntegerParameters.Add(TEXT("Seed"), 142);
		Source.NumericParameters.Add(TEXT("WorldSize"), 3200.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
		Source.NumericParameters.Add(TEXT("Frequency"), 0.0018);

		Recipe.Nodes.Add(Source);
		Recipe.OutputNode = Source.Id;

		if (bApplyThermal)
		{
			FEonformTerrainNode Thermal;
			Thermal.Id = FGuid(0x42000001, 0x42000002, 0x42000003, 0x42000004);
			Thermal.Type = EonformTerrainNodeTypes::ThermalErosion;
			Thermal.IntegerParameters.Add(TEXT("Duration"), 8);
			Thermal.NumericParameters.Add(TEXT("Angle"), 18.0);
			Thermal.NumericParameters.Add(TEXT("Strength"), 0.55);
			Thermal.NumericParameters.Add(TEXT("FeatureScale"), 250.0);
			Recipe.Nodes.Add(Thermal);

			FEonformTerrainConnection Connection;
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
	FEonformThermalErosionGraphTest,
	"Eonform.Core.Graph.ThermalErosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformThermalErosionGraphTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor ThermalDescriptor;
	TestTrue(
		TEXT("Thermal descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::ThermalErosion, ThermalDescriptor));
	TestEqual(TEXT("Thermal display name"), ThermalDescriptor.DisplayName, FString(TEXT("Thermal")));
	TestEqual(TEXT("Thermal has one input"), ThermalDescriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Thermal has terrain and Talus outputs"), ThermalDescriptor.Outputs.Num(), 2);
	TestEqual(TEXT("Thermal has eleven parameters"), ThermalDescriptor.Parameters.Num(), 11);

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Source = FEonformTerrainEvaluator::Evaluate(MakeThermalRecipe(false), Context);
	const FEonformTerrainEvaluationResult Thermal = FEonformTerrainEvaluator::Evaluate(MakeThermalRecipe(true), Context);

	TestTrue(TEXT("Perlin source evaluates"), Source.bSuccess);
	TestTrue(TEXT("Thermal evaluates"), Thermal.bSuccess);
	if (!Source.bSuccess || !Thermal.bSuccess)
	{
		if (!Source.bSuccess) AddError(Source.Error);
		if (!Thermal.bSuccess) AddError(Thermal.Error);
		return false;
	}

	const FEonformScalarField* SourceHeight = Source.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* ThermalHeight = Thermal.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Source has Height"), SourceHeight);
	TestNotNull(TEXT("Thermal output has Height"), ThermalHeight);
	if (!SourceHeight || !ThermalHeight) return false;

	TestEqual(TEXT("Thermal preserves Height domain"), ThermalHeight->Domain, SourceHeight->Domain);
	TestEqual(TEXT("Thermal preserves sample count"), ThermalHeight->Values.Num(), SourceHeight->Values.Num());

	double Difference = 0.0;
	for (int32 Index = 0; Index < ThermalHeight->Values.Num(); ++Index)
	{
		Difference += FMath::Abs(ThermalHeight->Values[Index] - SourceHeight->Values[Index]);
	}
	TestTrue(TEXT("Thermal changes the terrain"), Difference > 0.0001);

	return true;
}

#endif