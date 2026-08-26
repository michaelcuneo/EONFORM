#include "EonformGridDomain.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainFlatRoutingTest,
	"Eonform.Core.Hydrology.FlatRoutingUsesSpillDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainFlatRoutingTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 9;
	const FEonformGridDomain Domain = FEonformGridDomain::Make(
		FIntPoint(Resolution, Resolution),
		FVector2d(0.0, 0.0),
		FVector2d(800.0, 800.0));

	FEonformFieldDescriptor HeightDescriptor;
	HeightDescriptor.Name = EonformTerrainFieldNames::Height;
	HeightDescriptor.Unit = EEonformFieldUnit::Normalized;
	HeightDescriptor.Interpolation = EEonformInterpolation::Bilinear;

	FEonformScalarField Height;
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

	FEonformTerrainDataset Dataset;
	TestTrue(TEXT("Height field was published"), Dataset.SetScalarField(MoveTemp(Height)));

	const FEonformTerrainPhysicalMetrics Physical(8000.0, 8000.0, 1000.0, 0.0);
	FString Error;
	TestTrue(TEXT("Flat-routing hydrology solve succeeds"),
		FEonformTerrainDerivedData::EnsureHydrology(Dataset, 1000.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);

	const FEonformScalarField* Direction = Dataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
	const FEonformScalarField* Distance = Dataset.FindScalarField(EonformTerrainFieldNames::DistanceToOutletKm);
	TestNotNull(TEXT("FlowDirection exists"), Direction);
	TestNotNull(TEXT("DistanceToOutletKm exists"), Distance);
	if (!Direction || !Distance) return false;

	TestEqual(TEXT("Flow direction remains categorical"), Direction->Descriptor.Interpolation, EEonformInterpolation::Nearest);

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
