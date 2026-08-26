#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainAngleRoutingTest,
	"Eonform.Core.Graph.AngleRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainAngleRoutingTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Angle descriptor exists"),
		FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Angle, Descriptor));
	TestEqual(TEXT("Angle display name"), Descriptor.DisplayName, FString(TEXT("Angle")));
	TestEqual(TEXT("Angle category"), Descriptor.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("Angle input count"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Angle output count"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Angle parameter count"), Descriptor.Parameters.Num(), 3);
	if (Descriptor.Outputs.Num() == 1)
	{
		TestEqual(TEXT("Angle output name"), Descriptor.Outputs[0].Name, FName(TEXT("Mask")));
		TestEqual(TEXT("Angle output type"), Descriptor.Outputs[0].DataType, FName(TEXT("ScalarField")));
	}

	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(801, 1, 1, 1);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 8080);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FEonformTerrainNode Angle;
	Angle.Id = FGuid(802, 2, 2, 2);
	Angle.Type = EonformTerrainNodeTypes::Angle;
	Angle.NumericParameters.Add(TEXT("Azimuth"), 90.0);
	Angle.NumericParameters.Add(TEXT("RangeMin"), 0.0);
	Angle.NumericParameters.Add(TEXT("RangeMax"), 60.0);
	Angle.NumericParameters.Add(TEXT("Falloff"), 10.0);

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(803, 3, 3, 3);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Duration"), 8);
	Erosion.NumericParameters.Add(TEXT("Strength"), 1.25);
	Erosion.NumericParameters.Add(TEXT("Volume"), 1.5);
	Erosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("Erosion Strength"));
	Erosion.NumericParameters.Add(TEXT("Bias"), 0.0);

	Recipe.Nodes = { Source, Angle, Erosion };

	FEonformTerrainConnection SourceToAngle;
	SourceToAngle.FromNode = Source.Id;
	SourceToAngle.FromOutput = TEXT("Terrain");
	SourceToAngle.ToNode = Angle.Id;
	SourceToAngle.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToAngle);

	FEonformTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FEonformTerrainConnection AngleToArea;
	AngleToArea.FromNode = Angle.Id;
	AngleToArea.FromOutput = TEXT("Mask");
	AngleToArea.ToNode = Erosion.Id;
	AngleToArea.ToInput = TEXT("Area");
	Recipe.Connections.Add(AngleToArea);

	Recipe.OutputNode = Erosion.Id;

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Angle-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Angle-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Angle-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
