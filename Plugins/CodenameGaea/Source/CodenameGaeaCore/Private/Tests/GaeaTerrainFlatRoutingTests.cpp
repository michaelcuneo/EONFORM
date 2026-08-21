#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainFlatRoutingTest,
	"CodenameGaea.Core.Hydrology.FlatRoutingUsesSpillDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainFlatRoutingTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 9;
	const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
		FIntPoint(Resolution, Resolution),
		FVector2d(0.0, 0.0),
		FVector2d(800.0, 800.0));

	FGaeaFieldDescriptor HeightDescriptor;
	HeightDescriptor.Name = GaeaTerrainFieldNames::Height;
	HeightDescriptor.Unit = EGaeaFieldUnit::Normalized;
	HeightDescriptor.Interpolation = EGaeaInterpolation::Bilinear;

	FGaeaScalarField Height;
	Height.Initialize(Domain, HeightDescriptor);
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			// Raised rim around a broad flat depression. The east edge contains the
			// only low spill, so every interior flat cell must ultimately route there.
			const bool bBoundary = X == 0 || Y == 0 || X == Resolution - 1 || Y == Resolution - 1;
			Height.AtInterior(X, Y) = bBoundary ? 0.8f : 0.2f;
		}
	}
	Height.AtInterior(Resolution - 1, Resolution / 2) = 0.1f;

	FGaeaTerrainDataset Dataset;
	TestTrue(TEXT("Height field was published"), Dataset.SetScalarField(MoveTemp(Height)));

	const FGaeaTerrainPhysicalMetrics Physical(8000.0, 8000.0, 1000.0, 0.0);
	FString Error;
	TestTrue(TEXT("Flat-routing hydrology solve succeeds"),
		FGaeaTerrainDerivedData::EnsureHydrology(Dataset, 1000.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);

	const FGaeaScalarField* Direction = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection);
	const FGaeaScalarField* Distance = Dataset.FindScalarField(GaeaTerrainFieldNames::DistanceToOutletKm);
	TestNotNull(TEXT("FlowDirection exists"), Direction);
	TestNotNull(TEXT("DistanceToOutletKm exists"), Distance);
	if (!Direction || !Distance) return false;

	TestEqual(TEXT("Flow direction remains categorical"), Direction->Descriptor.Interpolation, EGaeaInterpolation::Nearest);

	int32 InteriorOutletCount = 0;
	for (int32 Y = 1; Y < Resolution - 1; ++Y)
	{
		for (int32 X = 1; X < Resolution - 1; ++X)
		{
			const float Code = Direction->AtInterior(X, Y);
			TestTrue(TEXT("D8 direction is discrete and in range"),
				FMath::IsNearlyEqual(Code, static_cast<float>(FMath::RoundToInt(Code))) && Code >= -1.0f && Code <= 7.0f);
			if (Code < 0.0f) ++InteriorOutletCount;
			TestTrue(TEXT("Interior routed cell has positive distance to outlet"), Distance->AtInterior(X, Y) > 0.0f);
		}
	}

	TestEqual(TEXT("Filled depression has no accidental interior outlets"), InteriorOutletCount, 0);
	return true;
}

#endif
