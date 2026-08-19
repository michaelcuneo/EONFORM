#include "Misc/AutomationTest.h"

#include "GaeaTerrainDataset.h"
#include "GaeaTerrainFieldNames.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainDatasetTest,
	"CodenameGaea.Core.TerrainDataset.CollectionAndSampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainDatasetTest::RunTest(const FString& Parameters)
{
	FGaeaFieldDescriptor HeightDescriptor;
	HeightDescriptor.Name = GaeaTerrainFieldNames::Height;
	HeightDescriptor.Unit = EGaeaFieldUnit::Normalized;
	HeightDescriptor.Interpolation = EGaeaInterpolation::Bilinear;

	FGaeaScalarField Height;
	Height.Initialize(
		FGaeaGridDomain::Make(
			FIntPoint(3, 3),
			FVector2d(-100.0, -100.0),
			FVector2d(100.0, 100.0)),
		HeightDescriptor);
	Height.AtInterior(1, 1) = 0.75f;

	FGaeaFieldDescriptor ClimateDescriptor;
	ClimateDescriptor.Name = GaeaTerrainFieldNames::Rainfall;
	ClimateDescriptor.Unit = EGaeaFieldUnit::Normalized;
	ClimateDescriptor.Interpolation = EGaeaInterpolation::Bilinear;

	FGaeaScalarField Rainfall;
	Rainfall.Initialize(
		FGaeaGridDomain::Make(
			FIntPoint(2, 2),
			FVector2d(-100.0, -100.0),
			FVector2d(100.0, 100.0)),
		ClimateDescriptor,
		0.25f);

	FGaeaTerrainDataset Dataset;
	TestTrue(TEXT("Dataset starts empty"), Dataset.IsEmpty());
	TestTrue(TEXT("Height field accepted"), Dataset.SetScalarField(Height));
	TestTrue(TEXT("Different-resolution rainfall field accepted"), Dataset.SetScalarField(MoveTemp(Rainfall)));
	TestEqual(TEXT("Dataset owns two scalar fields"), Dataset.NumScalarFields(), 2);
	TestTrue(TEXT("Height field is discoverable"), Dataset.HasScalarField(GaeaTerrainFieldNames::Height));
	TestTrue(TEXT("Rainfall field is discoverable"), Dataset.HasScalarField(GaeaTerrainFieldNames::Rainfall));

	float SampledHeight = 0.0f;
	TestTrue(
		TEXT("Dataset samples height by canonical name"),
		Dataset.SampleScalar(GaeaTerrainFieldNames::Height, FVector2d::ZeroVector, SampledHeight));
	TestTrue(TEXT("Height sample comes from owned field"), FMath::IsNearlyEqual(SampledHeight, 0.75f));

	TArray<FName> Names;
	Dataset.GetScalarFieldNames(Names);
	TestEqual(TEXT("Name enumeration includes both fields"), Names.Num(), 2);
	TestTrue(TEXT("Names are lexically ordered"), Names[0].LexicalLess(Names[1]));

	FGaeaScalarField Replacement = Height;
	Replacement.Fill(0.5f);
	TestTrue(TEXT("Replacing a named field succeeds"), Dataset.SetScalarField(Replacement));
	TestEqual(TEXT("Replacement does not grow field count"), Dataset.NumScalarFields(), 2);

	float ReplacedHeight = 0.0f;
	Dataset.SampleScalar(GaeaTerrainFieldNames::Height, FVector2d::ZeroVector, ReplacedHeight);
	TestTrue(TEXT("Replacement owns new values"), FMath::IsNearlyEqual(ReplacedHeight, 0.5f));

	TestFalse(TEXT("Missing field sample reports failure"), Dataset.SampleScalar(TEXT("Missing"), FVector2d::ZeroVector, ReplacedHeight));
	TestTrue(TEXT("Field removal succeeds"), Dataset.RemoveScalarField(GaeaTerrainFieldNames::Rainfall));
	TestEqual(TEXT("Field removal updates count"), Dataset.NumScalarFields(), 1);

	Dataset.Reset();
	TestTrue(TEXT("Reset empties dataset"), Dataset.IsEmpty());

	return true;
}

#endif
