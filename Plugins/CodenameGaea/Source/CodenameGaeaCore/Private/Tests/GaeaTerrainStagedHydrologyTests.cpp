#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainStagedHydrologyTest,
	"CodenameGaea.Core.Hydrology.StagedDemandDrivenProducts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainStagedHydrologyTest::RunTest(const FString& Parameters)
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
			Height.AtInterior(X, Y) = 1.0f
				- static_cast<float>(X) * 0.06f
				- static_cast<float>(Y) * 0.01f;
		}
	}
	// Include a small pit so the drainage-only stage still exercises the
	// depression-aware routing path rather than a trivial monotonic surface.
	Height.AtInterior(4, 4) -= 0.25f;

	FGaeaTerrainDataset Dataset;
	TestTrue(TEXT("Height published"), Dataset.SetScalarField(Height));
	const FGaeaTerrainPhysicalMetrics Physical(8000.0, 8000.0, 1500.0, 0.0);
	FString Error;

	TestTrue(TEXT("Drainage stage succeeds"),
		FGaeaTerrainDerivedData::EnsureDrainage(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Drainage publishes FlowDirection"), Dataset.HasScalarField(GaeaTerrainFieldNames::FlowDirection));
	TestFalse(TEXT("Drainage does not publish FlowAccumulation"), Dataset.HasScalarField(GaeaTerrainFieldNames::FlowAccumulation));
	TestFalse(TEXT("Drainage does not publish CatchmentAreaKm2"), Dataset.HasScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2));
	TestFalse(TEXT("Drainage does not publish DistanceToOutletKm"), Dataset.HasScalarField(GaeaTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Drainage does not publish StreamOrder"), Dataset.HasScalarField(GaeaTerrainFieldNames::StreamOrder));

	const FGaeaScalarField* DirectionBeforeUpgrade = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection);
	const float DirectionSample = DirectionBeforeUpgrade ? DirectionBeforeUpgrade->AtInterior(4, 4) : -99.0f;

	TestTrue(TEXT("Flow-analysis stage succeeds"),
		FGaeaTerrainDerivedData::EnsureFlowAnalysis(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Flow analysis publishes accumulation"), Dataset.HasScalarField(GaeaTerrainFieldNames::FlowAccumulation));
	TestTrue(TEXT("Flow analysis publishes catchment area"), Dataset.HasScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2));
	TestFalse(TEXT("Flow analysis leaves outlet distance lazy"), Dataset.HasScalarField(GaeaTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Flow analysis leaves stream order lazy"), Dataset.HasScalarField(GaeaTerrainFieldNames::StreamOrder));
	const FGaeaScalarField* DirectionAfterUpgrade = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection);
	TestTrue(TEXT("Cached drainage survives staged upgrade"),
		DirectionAfterUpgrade && FMath::IsNearlyEqual(DirectionAfterUpgrade->AtInterior(4, 4), DirectionSample));

	TestTrue(TEXT("Network stage succeeds"),
		FGaeaTerrainDerivedData::EnsureHydrologyNetwork(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Network stage publishes outlet distance"), Dataset.HasScalarField(GaeaTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Network stage still leaves stream order lazy"), Dataset.HasScalarField(GaeaTerrainFieldNames::StreamOrder));

	TestTrue(TEXT("Full hydrology stage succeeds"),
		FGaeaTerrainDerivedData::EnsureHydrology(Dataset, 1500.0f, Physical, &Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Full hydrology publishes stream order"), Dataset.HasScalarField(GaeaTerrainFieldNames::StreamOrder));

	// A second full request must be a no-op from the caller's perspective.
	const int32 FullFieldCount = Dataset.NumScalarFields();
	TestTrue(TEXT("Cached full hydrology request succeeds"),
		FGaeaTerrainDerivedData::EnsureHydrology(Dataset, 1500.0f, Physical, &Error));
	TestEqual(TEXT("Cached full request adds no fields"), Dataset.NumScalarFields(), FullFieldCount);

	FGaeaScalarField ReplacementHeight = Height;
	ReplacementHeight.AtInterior(0, 0) -= 0.01f;
	TestTrue(TEXT("Replacement Height published"), Dataset.SetScalarField(MoveTemp(ReplacementHeight)));
	TestFalse(TEXT("Height replacement invalidates FlowDirection"), Dataset.HasScalarField(GaeaTerrainFieldNames::FlowDirection));
	TestFalse(TEXT("Height replacement invalidates FlowAccumulation"), Dataset.HasScalarField(GaeaTerrainFieldNames::FlowAccumulation));
	TestFalse(TEXT("Height replacement invalidates CatchmentAreaKm2"), Dataset.HasScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2));
	TestFalse(TEXT("Height replacement invalidates DistanceToOutletKm"), Dataset.HasScalarField(GaeaTerrainFieldNames::DistanceToOutletKm));
	TestFalse(TEXT("Height replacement invalidates StreamOrder"), Dataset.HasScalarField(GaeaTerrainFieldNames::StreamOrder));

	return true;
}

#endif
