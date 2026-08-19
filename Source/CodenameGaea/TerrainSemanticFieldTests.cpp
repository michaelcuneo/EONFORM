#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "TerrainContext.h"
#include "TerrainGeology.h"
#include "TerrainHeightField.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaSemanticTerrainFieldsTest,
	"CodenameGaea.Legacy.SemanticFields.ContextGeologyMasks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSemanticTerrainFieldsTest::RunTest(const FString& Parameters)
{
	FTerrainHeightField HeightField;
	HeightField.Initialize(5, 400.0f);

	for (int32 Y = 0; Y < HeightField.Resolution; ++Y)
	{
		for (int32 X = 0; X < HeightField.Resolution; ++X)
		{
			HeightField.At(X, Y) = static_cast<float>(X + Y) / 8.0f;
		}
	}

	FTerrainContextMaps Context;
	FTerrainContext::Analyze(HeightField, 1000.0f, {}, {}, {}, Context);

	TestTrue(TEXT("Context fields validate against heightfield"), Context.IsValidFor(HeightField));
	TestEqual(TEXT("Elevation field name"), Context.ElevationField.Descriptor.Name, FName(TEXT("Elevation")));
	TestEqual(TEXT("Slope field unit"), static_cast<uint8>(Context.SlopeDegreesField.Descriptor.Unit), static_cast<uint8>(EGaeaFieldUnit::Degrees));
	TestTrue(TEXT("Elevation field domain matches heightfield"), Context.ElevationField.Domain == HeightField.GetGaeaDomain());
	TestTrue(TEXT("Elevation compatibility array shares field storage"), Context.Elevation.GetData() == Context.ElevationField.Values.GetData());

	FTerrainGeologyMaps Geology;
	FTerrainGeologySettings GeologySettings;
	FTerrainGeology::Build(HeightField, Context, nullptr, 1337, GeologySettings, Geology);

	TestTrue(TEXT("Geology fields validate against heightfield"), Geology.IsValidFor(HeightField));
	TestEqual(TEXT("Rock hardness field name"), Geology.RockHardnessField.Descriptor.Name, FName(TEXT("RockHardness")));
	TestEqual(TEXT("Soil depth field unit"), static_cast<uint8>(Geology.SoilDepthField.Descriptor.Unit), static_cast<uint8>(EGaeaFieldUnit::Normalized));
	TestTrue(TEXT("Rock hardness compatibility array shares field storage"), Geology.RockHardness.GetData() == Geology.RockHardnessField.Values.GetData());

	FTerrainProcessMaskSettings ProcessSettings;
	FTerrainProcessMasks Masks;
	FTerrainContext::BuildProcessMasks(Context, HeightField, 34.0f, ProcessSettings, Masks);

	TestTrue(TEXT("Process mask fields validate against heightfield"), Masks.IsValidFor(HeightField));
	TestEqual(TEXT("Rainfall field name"), Masks.RainfallField.Descriptor.Name, FName(TEXT("Rainfall")));
	TestTrue(TEXT("Rainfall compatibility array shares field storage"), Masks.Rainfall.GetData() == Masks.RainfallField.Values.GetData());

	return true;
}

#endif
