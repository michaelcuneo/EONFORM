#include "EonformGridDomain.h"
#include "EonformScalarField.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformGridDomainGuardBandTest,
	"Eonform.Core.GridDomain.GuardBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformGridDomainGuardBandTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = FEonformGridDomain::Make(
		FIntPoint(3, 3),
		FVector2d(-100.0, -100.0),
		FVector2d(100.0, 100.0),
		1);

	TestTrue(TEXT("Domain is valid"), Domain.IsValid());
	TestEqual(TEXT("Interior sample count"), Domain.GetInteriorSampleCount(), 9);
	TestEqual(TEXT("Storage dimensions"), Domain.GetStorageDimensions(), FIntPoint(5, 5));
	TestEqual(TEXT("Storage sample count"), Domain.GetStorageSampleCount(), 25);
	TestTrue(TEXT("Cell size is 100 x 100"), Domain.GetCellSize().Equals(FVector2d(100.0, 100.0), UE_DOUBLE_SMALL_NUMBER));
	TestTrue(TEXT("Evaluation min includes border"), Domain.GetEvaluationMin().Equals(FVector2d(-200.0, -200.0), UE_DOUBLE_SMALL_NUMBER));
	TestTrue(TEXT("Evaluation max includes border"), Domain.GetEvaluationMax().Equals(FVector2d(200.0, 200.0), UE_DOUBLE_SMALL_NUMBER));
	TestTrue(TEXT("Interior center maps to world origin"), Domain.InteriorSampleToWorld(1, 1).Equals(FVector2d::ZeroVector, UE_DOUBLE_SMALL_NUMBER));
	TestTrue(TEXT("Storage origin maps to guard-band minimum"), Domain.StorageSampleToWorld(0, 0).Equals(FVector2d(-200.0, -200.0), UE_DOUBLE_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformScalarFieldBilinearTest,
	"Eonform.Core.ScalarField.BilinearSampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformScalarFieldBilinearTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = FEonformGridDomain::Make(
		FIntPoint(2, 2),
		FVector2d(0.0, 0.0),
		FVector2d(100.0, 100.0));

	FEonformFieldDescriptor Descriptor;
	Descriptor.Name = TEXT("Test");
	Descriptor.Interpolation = EEonformInterpolation::Bilinear;

	FEonformScalarField Field;
	Field.Initialize(Domain, Descriptor);
	Field.AtInterior(0, 0) = 0.0f;
	Field.AtInterior(1, 0) = 10.0f;
	Field.AtInterior(0, 1) = 20.0f;
	Field.AtInterior(1, 1) = 30.0f;

	TestTrue(TEXT("Field is valid"), Field.IsValid());
	TestTrue(
		TEXT("Bilinear center sample averages four corners"),
		FMath::IsNearlyEqual(Field.SampleWorld(FVector2d(50.0, 50.0)), 15.0f));
	TestTrue(
		TEXT("Out-of-domain sample clamps when requested"),
		FMath::IsNearlyEqual(Field.SampleWorld(FVector2d(-50.0, -50.0), true), 0.0f));
	TestTrue(
		TEXT("Out-of-domain sample returns zero without clamping"),
		FMath::IsNearlyEqual(Field.SampleWorld(FVector2d(-50.0, -50.0), false), 0.0f));

	return true;
}

#endif
