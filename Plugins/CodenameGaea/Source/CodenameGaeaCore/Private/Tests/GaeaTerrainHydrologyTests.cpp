#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainHydrologyDrainageTest,
	"CodenameGaea.Core.Hydrology.DepressionAwareDrainage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainHydrologyDrainageTest::RunTest(const FString& Parameters)
{
	const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
		FIntPoint(5, 5),
		FVector2d(0.0, 0.0),
		FVector2d(400.0, 400.0));

	FGaeaFieldDescriptor HeightDescriptor;
	HeightDescriptor.Name = GaeaTerrainFieldNames::Height;
	HeightDescriptor.Unit = EGaeaFieldUnit::Normalized;
	HeightDescriptor.Interpolation = EGaeaInterpolation::Bilinear;

	FGaeaScalarField Height;
	Height.Initialize(Domain, HeightDescriptor);
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			// Overall fall toward the east edge with a deliberate interior pit.
			Height.AtInterior(X, Y) = 1.0f - static_cast<float>(X) * 0.15f;
		}
	}
	Height.AtInterior(2, 2) = 0.05f;

	FGaeaTerrainDataset Dataset;
	TestTrue(TEXT("Height field was published"), Dataset.SetScalarField(MoveTemp(Height)));

	FString Error;
	TestTrue(TEXT("Hydrology solve succeeds"), FGaeaTerrainDerivedData::EnsureHydrology(Dataset, 1000.0f, &Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}

	const FGaeaScalarField* Direction = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection);
	const FGaeaScalarField* Accumulation = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation);
	TestNotNull(TEXT("FlowDirection exists"), Direction);
	TestNotNull(TEXT("FlowAccumulation exists"), Accumulation);
	if (!Direction || !Accumulation)
	{
		return false;
	}

	TestTrue(TEXT("Direction field is valid"), Direction->IsValid());
	TestTrue(TEXT("Accumulation field is valid"), Accumulation->IsValid());
	TestEqual(TEXT("Direction uses nearest interpolation"), Direction->Descriptor.Interpolation, EGaeaInterpolation::Nearest);
	TestTrue(TEXT("Every cell contributes at least itself"), Accumulation->AtInterior(2, 2) >= 1.0f);

	float MaximumAccumulation = 0.0f;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			MaximumAccumulation = FMath::Max(MaximumAccumulation, Accumulation->AtInterior(X, Y));
		}
	}
	TestTrue(TEXT("Drainage creates a downstream accumulation signal"), MaximumAccumulation > 1.0f);

	const float PitDirection = Direction->AtInterior(2, 2);
	TestTrue(TEXT("Interior pit is routed rather than left as an outlet"), PitDirection >= 0.0f && PitDirection <= 7.0f);
	return true;
}

#endif
