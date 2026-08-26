#include "EonformGridDomain.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainPhysicalHydrologyTest,
	"Eonform.Core.Hydrology.PhysicalWorldMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainPhysicalHydrologyTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = FEonformGridDomain::Make(
		FIntPoint(5, 5),
		FVector2d(0.0, 0.0),
		FVector2d(400.0, 400.0));

	FEonformFieldDescriptor HeightDescriptor;
	HeightDescriptor.Name = EonformTerrainFieldNames::Height;
	HeightDescriptor.Unit = EEonformFieldUnit::Normalized;
	HeightDescriptor.Interpolation = EEonformInterpolation::Bilinear;

	FEonformScalarField Height;
	Height.Initialize(Domain, HeightDescriptor);
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			Height.AtInterior(X, Y) = 1.0f - static_cast<float>(X) * 0.15f - static_cast<float>(Y) * 0.01f;
		}
	}

	FEonformTerrainDataset Dataset;
	TestTrue(TEXT("Height field was published"), Dataset.SetScalarField(MoveTemp(Height)));

	const FEonformTerrainPhysicalMetrics Physical(
		10000.0, // 10 km width
		20000.0, // 20 km depth
		2500.0,  // 2.5 km elevation scale
		0.0);

	const FVector2d SampleSpacing = Physical.ResolveSampleSpacingMeters(FIntPoint(5, 5), FVector2d(100.0, 100.0));
	TestEqual(TEXT("Physical X sample spacing"), SampleSpacing.X, 2500.0);
	TestEqual(TEXT("Physical Y sample spacing"), SampleSpacing.Y, 5000.0);
	TestEqual(TEXT("Each hydrology sample represents equal physical area"), Physical.ResolveCellAreaSquareMeters(FIntPoint(5, 5), FVector2d(100.0, 100.0)), 8000000.0);

	FString Error;
	TestTrue(TEXT("Physical hydrology solve succeeds"), FEonformTerrainDerivedData::EnsureHydrology(Dataset, 1000.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);

	const FEonformScalarField* Accumulation = Dataset.FindScalarField(EonformTerrainFieldNames::FlowAccumulation);
	const FEonformScalarField* Catchment = Dataset.FindScalarField(EonformTerrainFieldNames::CatchmentAreaKm2);
	const FEonformScalarField* Distance = Dataset.FindScalarField(EonformTerrainFieldNames::DistanceToOutletKm);
	const FEonformScalarField* Order = Dataset.FindScalarField(EonformTerrainFieldNames::StreamOrder);

	TestNotNull(TEXT("FlowAccumulation exists"), Accumulation);
	TestNotNull(TEXT("CatchmentAreaKm2 exists"), Catchment);
	TestNotNull(TEXT("DistanceToOutletKm exists"), Distance);
	TestNotNull(TEXT("StreamOrder exists"), Order);
	if (!Accumulation || !Catchment || !Distance || !Order) return false;

	TestEqual(TEXT("Catchment area has square-kilometre units"), Catchment->Descriptor.Unit, EEonformFieldUnit::SquareKilometers);
	TestEqual(TEXT("Outlet distance has kilometre units"), Distance->Descriptor.Unit, EEonformFieldUnit::Kilometers);
	TestEqual(TEXT("Stream order uses nearest interpolation"), Order->Descriptor.Interpolation, EEonformInterpolation::Nearest);

	float MaximumDistance = 0.0f;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			const float SampleCount = Accumulation->AtInterior(X, Y);
			const float ExpectedAreaKm2 = SampleCount * 8.0f;
			TestTrue(TEXT("Catchment area matches contributing sample count and world area"),
				FMath::IsNearlyEqual(Catchment->AtInterior(X, Y), ExpectedAreaKm2, 0.001f));
			TestTrue(TEXT("Stream order is at least first order"), Order->AtInterior(X, Y) >= 1.0f);
			MaximumDistance = FMath::Max(MaximumDistance, Distance->AtInterior(X, Y));
		}
	}

	TestTrue(TEXT("Drainage network has non-zero physical path length"), MaximumDistance > 0.0f);
	return true;
}

#endif
