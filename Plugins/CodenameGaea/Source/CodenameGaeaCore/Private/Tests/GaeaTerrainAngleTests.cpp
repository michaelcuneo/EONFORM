#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainAngleRoutingTest,
	"CodenameGaea.Core.Graph.AngleRoutesToMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainAngleRoutingTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Angle descriptor exists"),
		FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Angle, Descriptor));
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

	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(801, 1, 1, 1);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 8080);
	Source.NumericParameters.Add(TEXT("WorldSize"), 2400.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 5000.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.0015);

	FGaeaTerrainNode Angle;
	Angle.Id = FGuid(802, 2, 2, 2);
	Angle.Type = GaeaTerrainNodeTypes::Angle;
	Angle.NumericParameters.Add(TEXT("Azimuth"), 90.0);
	Angle.NumericParameters.Add(TEXT("RangeMin"), 0.0);
	Angle.NumericParameters.Add(TEXT("RangeMax"), 60.0);
	Angle.NumericParameters.Add(TEXT("Falloff"), 10.0);

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(803, 3, 3, 3);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Duration"), 8);
	Erosion.NumericParameters.Add(TEXT("Strength"), 1.25);
	Erosion.NumericParameters.Add(TEXT("Volume"), 1.5);
	Erosion.NameParameters.Add(TEXT("AreaEffect"), TEXT("Erosion Strength"));
	Erosion.NumericParameters.Add(TEXT("Bias"), 0.0);

	Recipe.Nodes = { Source, Angle, Erosion };

	FGaeaTerrainConnection SourceToAngle;
	SourceToAngle.FromNode = Source.Id;
	SourceToAngle.FromOutput = TEXT("Terrain");
	SourceToAngle.ToNode = Angle.Id;
	SourceToAngle.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToAngle);

	FGaeaTerrainConnection SourceToErosion;
	SourceToErosion.FromNode = Source.Id;
	SourceToErosion.FromOutput = TEXT("Terrain");
	SourceToErosion.ToNode = Erosion.Id;
	SourceToErosion.ToInput = TEXT("Terrain");
	Recipe.Connections.Add(SourceToErosion);

	FGaeaTerrainConnection AngleToArea;
	AngleToArea.FromNode = Angle.Id;
	AngleToArea.FromOutput = TEXT("Mask");
	AngleToArea.ToNode = Erosion.Id;
	AngleToArea.ToInput = TEXT("Area");
	Recipe.Connections.Add(AngleToArea);

	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Angle-routed recipe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("Angle-routed result has Height"), Height);
	if (Height)
	{
		TestTrue(TEXT("Angle-routed Height is valid"), Height->IsValid());
	}
	return true;
}

#endif
