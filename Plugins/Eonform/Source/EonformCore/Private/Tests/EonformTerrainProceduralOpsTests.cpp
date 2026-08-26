#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainProceduralOps.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformGridDomain TestDomain(int32 W = 65, int32 H = 65)
	{
		return FEonformGridDomain::Make(FIntPoint(W, H), FVector2d(-1000.0, -1000.0), FVector2d(1000.0, 1000.0));
	}

	FEonformScalarField Field(const FEonformGridDomain& Domain, float Initial = 0.0f)
	{
		FEonformFieldDescriptor D;
		D.Name = EonformTerrainFieldNames::Height;
		D.Unit = EEonformFieldUnit::Normalized;
		D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField F;
		F.Initialize(Domain, D, Initial);
		return F;
	}

	double NeighborVariation(const FEonformScalarField& F)
	{
		double Sum = 0.0;
		int64 Count = 0;
		for (int32 Y = 0; Y < F.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < F.Domain.Dimensions.X; ++X)
			{
				if (X + 1 < F.Domain.Dimensions.X)
				{
					Sum += FMath::Abs(static_cast<double>(F.AtInterior(X + 1, Y) - F.AtInterior(X, Y)));
					++Count;
				}
				if (Y + 1 < F.Domain.Dimensions.Y)
				{
					Sum += FMath::Abs(static_cast<double>(F.AtInterior(X, Y + 1) - F.AtInterior(X, Y)));
					++Count;
				}
			}
		}
		return Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerraceGlobalLevelsTest,
	"Eonform.Core.ProceduralOps.TerraceGlobalLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerraceGlobalLevelsTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = TestDomain(17, 9);
	FEonformScalarField Source = Field(Domain);
	for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Domain.Dimensions.X; ++X)
		{
			Source.AtInterior(X, Y) = static_cast<float>(X) / static_cast<float>(Domain.Dimensions.X - 1);
		}
	}
	FEonformScalarField Result;
	FString Error;
	TestTrue(TEXT("Terrace evaluates"), EonformTerrainProceduralOps::ApplyTerrace(Source, 10, 0.6f, 0.2f, 1.0f, 4451, false, Result, &Error));
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
	FEonformDirectionalWarpMidpointTest,
	"Eonform.Core.ProceduralOps.DirectionalWarpMidpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformDirectionalWarpMidpointTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = TestDomain(31, 31);
	FEonformScalarField Source = Field(Domain);
	FEonformScalarField Guide = Field(Domain, 0.5f);
	for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Domain.Dimensions.X; ++X)
		{
			Source.AtInterior(X, Y) = static_cast<float>(X + Y) / 60.0f;
		}
	}
	FEonformScalarField Result;
	FString Error;
	TestTrue(TEXT("Directional warp evaluates"), EonformTerrainProceduralOps::DirectionWarpPixels(Source, Guide, 20.0f, 45.0f, EonformTerrainProceduralOps::EEdgeBehaviour::Mirror, Result, &Error));
	for (int32 I = 0; I < Source.Values.Num(); ++I)
	{
		TestTrue(TEXT("A 0.5 Custom guide produces zero displacement"), FMath::IsNearlyEqual(Source.Values[I], Result.Values[I], 1.e-6f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformRadialSmoothstepTest,
	"Eonform.Core.ProceduralOps.RadialSmoothstep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformRadialSmoothstepTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = TestDomain(101, 101);
	FEonformScalarField Result = Field(Domain, 1.0f);
	EonformTerrainProceduralOps::ApplyRadialGradientMultiply(Result, 50.0f, 50.0f, 40.0f, 1.0f);
	TestTrue(TEXT("Radial center remains one"), FMath::IsNearlyEqual(Result.AtInterior(50, 50), 1.0f, 1.e-6f));
	TestTrue(TEXT("Radial radius reaches zero"), FMath::IsNearlyEqual(Result.AtInterior(90, 50), 0.0f, 1.e-6f));
	TestTrue(TEXT("Radial falloff uses smoothstep profile"), FMath::IsNearlyEqual(Result.AtInterior(60, 50), 0.84375f, 1.e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformPerlinScaleDirectionTest,
	"Eonform.Core.ProceduralOps.PerlinScaleDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformPerlinScaleDirectionTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = TestDomain(129, 129);
	EonformTerrainProceduralOps::FPerlinSettings FineSettings;
	FineSettings.Scale = 0.10f;
	FineSettings.Octaves = 4;
	FineSettings.Gain = 0.5f;
	FineSettings.Seed = 4451;
	FineSettings.WarpType = TEXT("None");

	EonformTerrainProceduralOps::FPerlinSettings BroadSettings = FineSettings;
	BroadSettings.Scale = 0.90f;

	FEonformScalarField Fine;
	FEonformScalarField Broad;
	FString Error;
	TestTrue(TEXT("Fine-scale Perlin evaluates"), EonformTerrainProceduralOps::GeneratePerlin(Domain, FineSettings, Fine, &Error));
	TestTrue(TEXT("Broad-scale Perlin evaluates"), EonformTerrainProceduralOps::GeneratePerlin(Domain, BroadSettings, Broad, &Error));
	TestTrue(TEXT("RawNoise Perlin Scale increases perceptual feature size"), NeighborVariation(Fine) > NeighborVariation(Broad));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformVoronoiPDistance2AddTest,
	"Eonform.Core.ProceduralOps.VoronoiPDistance2Add",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformVoronoiPDistance2AddTest::RunTest(const FString& Parameters)
{
	const FEonformGridDomain Domain = TestDomain(97, 97);
	EonformTerrainProceduralOps::FVoronoiSettings PSettings;
	PSettings.Scale = 0.55f;
	PSettings.Form = TEXT("P");
	PSettings.Function = TEXT("Euclidean");
	PSettings.WarpType = TEXT("None");
	PSettings.Seed = 4451;
	PSettings.Jitter = 0.45f;

	EonformTerrainProceduralOps::FVoronoiSettings RSettings = PSettings;
	RSettings.Form = TEXT("R");

	FEonformScalarField P;
	FEonformScalarField R;
	FString Error;
	TestTrue(TEXT("Voronoi P evaluates"), EonformTerrainProceduralOps::GenerateVoronoi(Domain, PSettings, P, &Error));
	TestTrue(TEXT("Voronoi R evaluates"), EonformTerrainProceduralOps::GenerateVoronoi(Domain, RSettings, R, &Error));

	double MeanP = 0.0;
	double MeanR = 0.0;
	for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Domain.Dimensions.X; ++X)
		{
			MeanP += P.AtInterior(X, Y);
			MeanR += R.AtInterior(X, Y);
		}
	}
	const double Count = static_cast<double>(Domain.GetInteriorSampleCount());
	MeanP /= Count;
	MeanR /= Count;
	TestTrue(TEXT("Voronoi P is the Distance2Add family, not the Distance field"), MeanP > MeanR);
	return true;
}

#endif
