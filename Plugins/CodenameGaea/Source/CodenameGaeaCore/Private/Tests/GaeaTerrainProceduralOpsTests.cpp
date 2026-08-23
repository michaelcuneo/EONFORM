#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainProceduralOps.h"
#include "GaeaTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaGridDomain TestDomain(int32 W = 65, int32 H = 65)
	{
		return FGaeaGridDomain::Make(FIntPoint(W, H), FVector2d(-1000.0, -1000.0), FVector2d(1000.0, 1000.0));
	}

	FGaeaScalarField Field(const FGaeaGridDomain& Domain, float Initial = 0.0f)
	{
		FGaeaFieldDescriptor D;
		D.Name = GaeaTerrainFieldNames::Height;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField F;
		F.Initialize(Domain, D, Initial);
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerraceGlobalLevelsTest,
	"CodenameGaea.Core.ProceduralOps.TerraceGlobalLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerraceGlobalLevelsTest::RunTest(const FString& Parameters)
{
	const FGaeaGridDomain Domain = TestDomain(17, 9);
	FGaeaScalarField Source = Field(Domain);
	for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Domain.Dimensions.X; ++X)
		{
			Source.AtInterior(X, Y) = static_cast<float>(X) / static_cast<float>(Domain.Dimensions.X - 1);
		}
	}
	FGaeaScalarField Result;
	FString Error;
	TestTrue(TEXT("Terrace evaluates"), GaeaTerrainProceduralOps::ApplyTerrace(Source, 10, 0.6f, 0.2f, 1.0f, 4451, false, Result, &Error));
	for (int32 X = 0; X < Domain.Dimensions.X; ++X)
	{
		const float Expected = Result.AtInterior(X, 0);
		for (int32 Y = 1; Y < Domain.Dimensions.Y; ++Y)
		{
			TestTrue(TEXT("Equal source values use the same global terrace profile"), FMath::IsNearlyEqual(Result.AtInterior(X, Y), Expected, 1.e-6f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaDirectionalWarpMidpointTest,
	"CodenameGaea.Core.ProceduralOps.DirectionalWarpMidpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaDirectionalWarpMidpointTest::RunTest(const FString& Parameters)
{
	const FGaeaGridDomain Domain = TestDomain(31, 31);
	FGaeaScalarField Source = Field(Domain);
	FGaeaScalarField Guide = Field(Domain, 0.5f);
	for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Domain.Dimensions.X; ++X)
		{
			Source.AtInterior(X, Y) = static_cast<float>(X + Y) / 60.0f;
		}
	}
	FGaeaScalarField Result;
	FString Error;
	TestTrue(TEXT("Directional warp evaluates"), GaeaTerrainProceduralOps::DirectionWarpPixels(Source, Guide, 20.0f, 45.0f, GaeaTerrainProceduralOps::EEdgeBehaviour::Mirror, Result, &Error));
	for (int32 I = 0; I < Source.Values.Num(); ++I)
	{
		TestTrue(TEXT("A 0.5 Custom guide produces zero displacement"), FMath::IsNearlyEqual(Source.Values[I], Result.Values[I], 1.e-6f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaRadialSmoothstepTest,
	"CodenameGaea.Core.ProceduralOps.RadialSmoothstep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaRadialSmoothstepTest::RunTest(const FString& Parameters)
{
	const FGaeaGridDomain Domain = TestDomain(101, 101);
	FGaeaScalarField Result = Field(Domain, 1.0f);
	GaeaTerrainProceduralOps::ApplyRadialGradientMultiply(Result, 50.0f, 50.0f, 40.0f, 1.0f);
	TestTrue(TEXT("Radial center remains one"), FMath::IsNearlyEqual(Result.AtInterior(50, 50), 1.0f, 1.e-6f));
	TestTrue(TEXT("Radial radius reaches zero"), FMath::IsNearlyEqual(Result.AtInterior(90, 50), 0.0f, 1.e-6f));
	TestTrue(TEXT("Radial falloff uses smoothstep profile"), FMath::IsNearlyEqual(Result.AtInterior(60, 50), 0.84375f, 1.e-5f));
	return true;
}

#endif
