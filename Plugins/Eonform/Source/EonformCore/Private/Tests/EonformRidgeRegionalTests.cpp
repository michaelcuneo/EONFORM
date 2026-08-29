#if WITH_DEV_AUTOMATION_TESTS

#include "EonformRidgeNode.h"
#include "EonformTerrainEvaluator.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRidgeStreamedMatchesLegacyTest,
	"Eonform.Core.RegionalEvaluation.RidgeStreamedMatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRidgeStreamedMatchesLegacyTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = FEonformGridDomain::Make(
		FIntPoint(33, 33),
		FVector2d(-16000.0, -16000.0),
		FVector2d(16000.0, 16000.0));

	FEonformRidgeSettings Settings;
	Settings.Scale = 0.72f;
	Settings.Height = 0.83f;
	Settings.Definition = 0.47f;
	Settings.Seed = 7281;
	Settings.ScaleX = 1.13f;
	Settings.ScaleY = 0.91f;

	FEonformScalarField Legacy;
	FString Error;
	TestTrue(TEXT("Legacy Ridge evaluates"), FEonformRidgeGenerator::Generate(Domain, Settings, Legacy, &Error));
	if (!Legacy.IsValid())
	{
		AddError(Error);
		return false;
	}

	FEonformScalarField Streamed;
	const TSharedPtr<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe> SummaryCache =
		MakeShared<FEonformTerrainGlobalSummaryCache, ESPMode::ThreadSafe>();
	TestTrue(
		TEXT("Streamed Ridge evaluates"),
		FEonformRidgeGenerator::GenerateRegional(
			Domain,
			Domain,
			Settings,
			false,
			SummaryCache,
			0x5249444745544553ull,
			Streamed,
			&Error));
	if (!Streamed.IsValid())
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Streamed sample count matches legacy"), Streamed.Values.Num(), Legacy.Values.Num());
	if (Streamed.Values.Num() != Legacy.Values.Num()) return false;

	for (int32 I = 0; I < Legacy.Values.Num(); ++I)
	{
		if (!FMath::IsNearlyEqual(Legacy.Values[I], Streamed.Values[I], 1.e-6f))
		{
			AddError(FString::Printf(
				TEXT("Ridge differs at sample %d: legacy %.9f streamed %.9f delta %.9g"),
				I,
				Legacy.Values[I],
				Streamed.Values[I],
				FMath::Abs(Legacy.Values[I] - Streamed.Values[I])));
			return false;
		}
	}

	return true;
}

#endif
