#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "TerrainErosion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformHydraulicErosionOutputsTest,
	"Eonform.Legacy.Erosion.HydraulicOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformHydraulicErosionOutputsTest::RunTest(const FString& Parameters)
{
	FTerrainHeightField Input;
	Input.Initialize(9, 800.0f);

	for (int32 Y = 0; Y < Input.Resolution; ++Y)
	{
		for (int32 X = 0; X < Input.Resolution; ++X)
		{
			const float NX = static_cast<float>(X) / static_cast<float>(Input.Resolution - 1);
			const float NY = static_cast<float>(Y) / static_cast<float>(Input.Resolution - 1);
			const float Peak = 0.45f * FMath::Max(0.0f, 1.0f - FVector2D(NX - 0.5f, NY - 0.5f).Size() * 2.0f);
			Input.At(X, Y) = 0.15f * NX + Peak;
		}
	}

	const TArray<float> OriginalValues = Input.Data;

	FTerrainHydraulicErosionSettings Settings;
	Settings.Iterations = 6;
	Settings.Rainfall = 0.02f;
	Settings.FlowRate = 0.6f;
	Settings.SedimentCapacity = 0.8f;
	Settings.ErosionRate = 0.2f;
	Settings.DepositionRate = 0.15f;
	Settings.Evaporation = 0.05f;

	FTerrainHydraulicErosionResult Result;
	TestTrue(TEXT("EvaluateHydraulic succeeds"), FTerrainErosion::EvaluateHydraulic(Input, 1000.0f, Settings, Result));
	TestTrue(TEXT("Hydraulic result is valid"), Result.IsValid());

	TestEqual(TEXT("Input sample count is unchanged"), Input.Data.Num(), OriginalValues.Num());
	for (int32 Index = 0; Index < Input.Data.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(Input.Data[Index], OriginalValues[Index]))
		{
			AddError(FString::Printf(TEXT("EvaluateHydraulic mutated input at index %d"), Index));
			break;
		}
	}

	TestEqual(TEXT("Height field name"), Result.Height.Descriptor.Name, FName(TEXT("Height")));
	TestEqual(TEXT("Wear field name"), Result.Wear.Descriptor.Name, FName(TEXT("Wear")));
	TestEqual(TEXT("Deposits field name"), Result.Deposits.Descriptor.Name, FName(TEXT("Deposits")));
	TestEqual(TEXT("Flow field name"), Result.Flow.Descriptor.Name, FName(TEXT("Flow")));

	FTerrainHeightField Legacy = Input;
	TArray<float> LegacyFlow;
	TArray<float> LegacyWear;
	TArray<float> LegacyDeposits;
	FTerrainErosion::ApplyHydraulic(
		Legacy,
		1000.0f,
		Settings,
		&LegacyFlow,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		&LegacyWear,
		&LegacyDeposits);

	TestEqual(TEXT("Legacy and result height sample counts match"), Legacy.Data.Num(), Result.Height.Values.Num());
	TestEqual(TEXT("Legacy and result flow sample counts match"), LegacyFlow.Num(), Result.Flow.Values.Num());

	for (int32 Index = 0; Index < Legacy.Data.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(Legacy.Data[Index], Result.Height.Values[Index])
			|| !FMath::IsNearlyEqual(LegacyFlow[Index], Result.Flow.Values[Index])
			|| !FMath::IsNearlyEqual(LegacyWear[Index], Result.Wear.Values[Index])
			|| !FMath::IsNearlyEqual(LegacyDeposits[Index], Result.Deposits.Values[Index]))
		{
			AddError(FString::Printf(TEXT("Hydraulic legacy/result mismatch at index %d"), Index));
			break;
		}
	}

	return true;
}

#endif
