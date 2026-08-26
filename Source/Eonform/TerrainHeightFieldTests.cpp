#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "TerrainHeightField.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainHeightFieldSharedStorageTest,
	"Eonform.Legacy.HeightField.SharedStorage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainHeightFieldSharedStorageTest::RunTest(const FString& Parameters)
{
	FTerrainHeightField HeightField;
	HeightField.Initialize(3, 200.0f);

	TestTrue(TEXT("Heightfield should be valid after initialization"), HeightField.IsValid());
	TestEqual(TEXT("Legacy data count should match grid"), HeightField.Data.Num(), 9);

	HeightField.Data[HeightField.Index(1, 1)] = 0.25f;
	TestEqual(TEXT("Legacy Data write should update scalar field storage"), HeightField.GetEonformField().AtInterior(1, 1), 0.25f);

	HeightField.At(2, 0) = 0.75f;
	TestEqual(TEXT("At write should update legacy Data alias"), HeightField.Data[HeightField.Index(2, 0)], 0.75f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainHeightFieldValueSemanticsTest,
	"Eonform.Legacy.HeightField.ValueSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainHeightFieldValueSemanticsTest::RunTest(const FString& Parameters)
{
	FTerrainHeightField Original;
	Original.Initialize(3, 200.0f);
	Original.At(1, 1) = 0.4f;

	FTerrainHeightField Copy = Original;
	TestTrue(TEXT("Copied heightfield should be valid"), Copy.IsValid());
	TestEqual(TEXT("Copy should preserve values"), Copy.At(1, 1), 0.4f);

	Copy.At(1, 1) = 0.9f;
	TestEqual(TEXT("Changing copy must not mutate original"), Original.At(1, 1), 0.4f);

	FTerrainHeightField Moved = MoveTemp(Copy);
	TestTrue(TEXT("Moved heightfield should be valid"), Moved.IsValid());
	TestEqual(TEXT("Move should preserve values"), Moved.At(1, 1), 0.9f);

	return true;
}

#endif
