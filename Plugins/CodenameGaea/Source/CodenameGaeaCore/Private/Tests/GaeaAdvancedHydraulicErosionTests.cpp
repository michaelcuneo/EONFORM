#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaHydraulicErosion.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaAdvancedHydraulicChannelFormationTest,
	"CodenameGaea.Core.Hydraulic.AdvancedChannelFormation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaAdvancedHydraulicChannelFormationTest::RunTest(const FString& Parameters)
{
	const int32 Resolution = 129;
	const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
		FIntPoint(Resolution, Resolution),
		FVector2d(0.0, 0.0),
		FVector2d(100000.0, 100000.0));
	TestTrue(TEXT("Hydraulic test domain is valid"), Domain.IsValid());
	if (!Domain.IsValid()) return false;

	FGaeaFieldDescriptor Descriptor;
	Descriptor.Name = GaeaTerrainFieldNames::Height;
	Descriptor.Unit = EGaeaFieldUnit::Normalized;
	Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
	FGaeaScalarField Height;
	Height.Initialize(Domain, Descriptor, 0.0f);

	FRandomStream Random(91827);
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const float V = static_cast<float>(Y) / static_cast<float>(Resolution - 1);
		for (int32 X = 0; X < Resolution; ++X)
		{
			const float U = static_cast<float>(X) / static_cast<float>(Resolution - 1);
			const float DX = U - 0.5f;
			const float DY = V - 0.52f;
			const float Radius = FMath::Sqrt(DX * DX + DY * DY);
			const float Massif = FMath::Clamp(1.0f - Radius * 1.7f, 0.0f, 1.0f);
			const float RegionalSlope = (1.0f - V) * 0.16f;
			const float Roughness = Random.FRandRange(-0.008f, 0.008f);
			Height.AtInterior(X, Y) = FMath::Max(Massif * 0.74f + RegionalSlope + Roughness, 0.0f);
		}
	}
	const FGaeaScalarField Before = Height;

	FGaeaHydraulicErosionSettings Settings;
	Settings.bAdvancedFlowSolver = true;
	Settings.Iterations = 18;
	Settings.Strength = 0.9f;
	Settings.Downcutting = 0.72f;
	Settings.FeatureScale = 0.8f;
	Settings.Volume = 1.15f;
	Settings.Debris = 0.35f;
	Settings.SedimentCapacity = 0.85f;
	Settings.ErosionRate = 0.28f;
	Settings.DepositionRate = 0.14f;
	Settings.SedimentRemoval = 0.08f;
	Settings.PhysicalSampleSpacingMeters = 1000.0 / static_cast<double>(Resolution - 1);
	Settings.PhysicalElevationScaleMeters = 1800.0;
	Settings.Seed = 91827;

	TArray<float> Flow;
	TArray<float> Wear;
	TArray<float> Deposits;
	TestTrue(
		TEXT("Advanced hydraulic solver succeeds"),
		FGaeaHydraulicErosion::ApplyInPlace(
			Height,
			180000.0f,
			Settings,
			&Flow,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			&Wear,
			&Deposits));

	TestEqual(TEXT("Flow output matches terrain samples"), Flow.Num(), Height.Values.Num());
	TestEqual(TEXT("Wear output matches terrain samples"), Wear.Num(), Height.Values.Num());
	TestEqual(TEXT("Deposit output matches terrain samples"), Deposits.Num(), Height.Values.Num());

	float MaxFlow = 0.0f;
	float MeanFlow = 0.0f;
	float TotalWear = 0.0f;
	float TotalChange = 0.0f;
	bool bFinite = true;
	for (int32 I = 0; I < Height.Values.Num(); ++I)
	{
		bFinite &= FMath::IsFinite(Height.Values[I]);
		bFinite &= FMath::IsFinite(Flow[I]);
		bFinite &= FMath::IsFinite(Wear[I]);
		bFinite &= FMath::IsFinite(Deposits[I]);
		MaxFlow = FMath::Max(MaxFlow, Flow[I]);
		MeanFlow += Flow[I];
		TotalWear += Wear[I];
		TotalChange += FMath::Abs(Height.Values[I] - Before.Values[I]);
	}
	MeanFlow /= FMath::Max(static_cast<float>(Flow.Num()), 1.0f);

	TestTrue(TEXT("Advanced erosion remains finite"), bFinite);
	TestTrue(TEXT("Advanced erosion modifies the terrain"), TotalChange > 0.01f);
	TestTrue(TEXT("Advanced erosion creates channel wear"), TotalWear > 0.005f);
	TestTrue(TEXT("Runoff converges into concentrated drainage"), MaxFlow > MeanFlow * 5.0f);
	return true;
}

#endif
