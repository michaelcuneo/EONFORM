#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainDerivedData.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainStagedHydrologyTest,
	"Eonform.Core.Hydrology.StagedDemandDrivenProducts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainStagedHydrologyTest::RunTest(const FString& Parameters)
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
			Height.AtInterior(X, Y) = 1.0f
				- static_cast<float>(X) * 0.06f
				- static_cast<float>(Y) * 0.01f;
		}
	}
	// Include a small pit so the drainage-only stage still exercises the
	// depression-aware routing path rather than a trivial monotonic surface.
	Height.AtInterior(4, 4) -= 0.25f;

	FEonformTerrainDataset Dataset;
	TestTrue(TEXT("Height published"), Dataset.SetScalarField(Height));
	const FEonformTerrainPhysicalMetrics Physical(8000.0, 8000.0, 1500.0, 0.0);
	FString Error;

	TestTrue(TEXT("Drainage stage succeeds"),
		FEonformTerrainDerivedData::EnsureDrainage(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Drainage publishes FlowDirection"), Dataset.HasScalarField(EonformTerrainFieldNames::FlowDirection));
	TestFalse(TEXT("Drainage does not publish FlowAccumulation"), Dataset.HasScalarField(EonformTerrainFieldNames::FlowAccumulation));
	TestFalse(TEXT("Drainage does not publish CatchmentAreaKm2"), Dataset.HasScalarField(EonformTerrainFieldNames::CatchmentAreaKm2));
	TestFalse(TEXT("Drainage does not publish DistanceToOutletKm"), Dataset.HasScalarField(EonformTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Drainage does not publish StreamOrder"), Dataset.HasScalarField(EonformTerrainFieldNames::StreamOrder));

	const FEonformScalarField* DirectionBeforeUpgrade = Dataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
	const float DirectionSample = DirectionBeforeUpgrade ? DirectionBeforeUpgrade->AtInterior(4, 4) : -99.0f;

	TestTrue(TEXT("Flow-analysis stage succeeds"),
		FEonformTerrainDerivedData::EnsureFlowAnalysis(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Flow analysis publishes accumulation"), Dataset.HasScalarField(EonformTerrainFieldNames::FlowAccumulation));
	TestTrue(TEXT("Flow analysis publishes catchment area"), Dataset.HasScalarField(EonformTerrainFieldNames::CatchmentAreaKm2));
	TestFalse(TEXT("Flow analysis leaves outlet distance lazy"), Dataset.HasScalarField(EonformTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Flow analysis leaves stream order lazy"), Dataset.HasScalarField(EonformTerrainFieldNames::StreamOrder));
	const FEonformScalarField* DirectionAfterUpgrade = Dataset.FindScalarField(EonformTerrainFieldNames::FlowDirection);
	TestTrue(TEXT("Cached drainage survives staged upgrade"),
		DirectionAfterUpgrade && FMath::IsNearlyEqual(DirectionAfterUpgrade->AtInterior(4, 4), DirectionSample));

	TestTrue(TEXT("Network stage succeeds"),
		FEonformTerrainDerivedData::EnsureHydrologyNetwork(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Network stage publishes outlet distance"), Dataset.HasScalarField(EonformTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Network stage still leaves stream order lazy"), Dataset.HasScalarField(EonformTerrainFieldNames::StreamOrder));

	TestTrue(TEXT("Full hydrology stage succeeds"),
		FEonformTerrainDerivedData::EnsureHydrology(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Full hydrology publishes stream order"), Dataset.HasScalarField(EonformTerrainFieldNames::StreamOrder));

	// A second full request must be a no-op from the caller's perspective.
	const int32 FullFieldCount = Dataset.NumScalarFields();
	TestTrue(TEXT("Cached full hydrology request succeeds"),
		FEonformTerrainDerivedData::EnsureHydrology(Dataset, 1500.0f, Physical, &Error));
	TestEqual(TEXT("Cached full request adds no fields"), Dataset.NumScalarFields(), FullFieldCount);

	FEonformScalarField ReplacementHeight = Height;
	ReplacementHeight.AtInterior(0, 0) -= 0.01f;
	TestTrue(TEXT("Replacement Height published"), Dataset.SetScalarField(MoveTemp(ReplacementHeight)));
	TestFalse(TEXT("Height replacement invalidates FlowDirection"), Dataset.HasScalarField(EonformTerrainFieldNames::FlowDirection));
	TestFalse(TEXT("Height replacement invalidates FlowAccumulation"), Dataset.HasScalarField(EonformTerrainFieldNames::FlowAccumulation));
	TestFalse(TEXT("Height replacement invalidates CatchmentAreaKm2"), Dataset.HasScalarField(EonformTerrainFieldNames::CatchmentAreaKm2));
	TestFalse(TEXT("Height replacement invalidates DistanceToOutletKm"), Dataset.HasScalarField(EonformTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Height replacement invalidates StreamOrder"), Dataset.HasScalarField(EonformTerrainFieldNames::StreamOrder));

	return true;
}

#endif
