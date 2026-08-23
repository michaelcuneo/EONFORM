#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainSemanticNodes.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaScalarField MakeSemanticHeight(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor, 0.0f);

		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const float NX = static_cast<float>(X) / static_cast<float>(Domain.Dimensions.X - 1);
				const float NY = static_cast<float>(Y) / static_cast<float>(Domain.Dimensions.Y - 1);
				const float DX = (NX - 0.5f) / 0.24f;
				const float DY = (NY - 0.32f) / 0.28f;
				const float Mountain = 0.72f * FMath::Exp(-(DX * DX + DY * DY) * 1.35f);
				const float Valley = -0.18f * FMath::Exp(-FMath::Square((NX - 0.5f) / 0.10f));
				const float RegionalFall = 0.22f * (1.0f - NY);
				Height.AtInterior(X, Y) = FMath::Clamp(-0.10f + Mountain + Valley + RegionalFall, -0.9f, 0.95f);
			}
		}
		return Height;
	}

	FGaeaTerrainEvaluationContext MakeSemanticContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(8000.0, 8000.0, 1800.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(41, 41),
			FVector2d(-400000.0, -400000.0),
			FVector2d(400000.0, 400000.0));
		Context.SourceDataset.SetScalarField(MakeSemanticHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeSingleSemanticRecipe(FName Type)
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(4101, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;
		FGaeaTerrainNode Semantic;
		Semantic.Id = FGuid(4102, 2, 2, GetTypeHash(Type));
		Semantic.Type = Type;
		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Semantic.Id;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Nodes = { Source, Semantic };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = Semantic.Id;
		return Recipe;
	}

	FGaeaTerrainRecipe MakeRockSoilRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(4201, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;
		FGaeaTerrainNode Rock;
		Rock.Id = FGuid(4202, 2, 2, 2);
		Rock.Type = GaeaTerrainNodeTypes::RockMap;
		Rock.NumericParameters.Add(TEXT("Exposure"), 0.72);
		Rock.NumericParameters.Add(TEXT("Steepness"), 0.68);
		FGaeaTerrainNode Soil;
		Soil.Id = FGuid(4203, 3, 3, 3);
		Soil.Type = GaeaTerrainNodeTypes::Soil;
		Soil.NumericParameters.Add(TEXT("Coverage"), 0.82);
		Soil.NumericParameters.Add(TEXT("ValleyBias"), 0.76);

		FGaeaTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Terrain");
		A.ToNode = Rock.Id;
		A.ToInput = TEXT("Terrain");
		FGaeaTerrainConnection B;
		B.FromNode = Rock.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Soil.Id;
		B.ToInput = TEXT("Terrain");
		Recipe.Nodes = { Source, Rock, Soil };
		Recipe.Connections = { A, B };
		Recipe.OutputNode = Soil.Id;
		return Recipe;
	}

	bool ValidateNormalizedField(FAutomationTestBase& Test, const FGaeaTerrainEvaluationResult& Result, FName FieldName, float& OutTotal)
	{
		Test.TestTrue(TEXT("Semantic graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}
		const FGaeaScalarField* Field = Result.Dataset.FindScalarField(FieldName);
		Test.TestNotNull(*FString::Printf(TEXT("%s semantic exists"), *FieldName.ToString()), Field);
		if (!Field) return false;
		OutTotal = 0.0f;
		for (int32 Y = 0; Y < Field->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field->Domain.Dimensions.X; ++X)
			{
				const float V = Field->AtInterior(X, Y);
				Test.TestTrue(TEXT("Semantic values remain finite"), FMath::IsFinite(V));
				Test.TestTrue(TEXT("Semantic values remain normalized"), V >= 0.0f && V <= 1.0f);
				OutTotal += V;
			}
		}
		Test.TestTrue(TEXT("Semantic field is non-trivial"), OutTotal > 0.05f);
		return true;
	}

	void VerifyInvalidation(FAutomationTestBase& Test, const FGaeaTerrainEvaluationResult& Result, std::initializer_list<FName> Names)
	{
		FGaeaTerrainDataset Dataset = Result.Dataset;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height) return;
		FGaeaScalarField Replacement = *Height;
		Replacement.AtInterior(Replacement.Domain.Dimensions.X / 2, Replacement.Domain.Dimensions.Y / 2) += 0.002f;
		Test.TestTrue(TEXT("Replacement Height publishes"), Dataset.SetScalarField(MoveTemp(Replacement)));
		for (const FName Name : Names)
		{
			Test.TestFalse(*FString::Printf(TEXT("%s invalidates with Height"), *Name.ToString()), Dataset.HasScalarField(Name));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainSemanticDeriveTest,
	"CodenameGaea.Core.Graph.TerrainSemanticDerive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainSemanticDeriveTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor PeaksDescriptor;
	FGaeaTerrainNodeDescriptor RockDescriptor;
	FGaeaTerrainNodeDescriptor SoilDescriptor;
	TestTrue(TEXT("Peaks descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Peaks, PeaksDescriptor));
	TestTrue(TEXT("RockMap descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::RockMap, RockDescriptor));
	TestTrue(TEXT("Soil descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Soil, SoilDescriptor));
	TestEqual(TEXT("Peaks parameter contract"), PeaksDescriptor.Parameters.Num(), 2);
	TestEqual(TEXT("RockMap parameter contract"), RockDescriptor.Parameters.Num(), 2);
	TestEqual(TEXT("Soil parameter contract"), SoilDescriptor.Parameters.Num(), 2);

	const FGaeaTerrainEvaluationContext Context = MakeSemanticContext();
	const FGaeaTerrainEvaluationResult PeaksResult = FGaeaTerrainEvaluator::Evaluate(MakeSingleSemanticRecipe(GaeaTerrainNodeTypes::Peaks), Context);
	float PeaksTotal = 0.0f;
	if (!ValidateNormalizedField(*this, PeaksResult, GaeaTerrainFieldNames::Peaks, PeaksTotal)) return false;

	const FGaeaScalarField* Peaks = PeaksResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Peaks);
	const FGaeaScalarField* Elevation = PeaksResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
	if (Peaks && Elevation)
	{
		float HighPeakSum = 0.0f;
		float LowPeakSum = 0.0f;
		int32 HighCount = 0;
		int32 LowCount = 0;
		for (int32 Y = 0; Y < Peaks->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Peaks->Domain.Dimensions.X; ++X)
			{
				const float E = Elevation->AtInterior(X, Y);
				if (E > 0.65f) { HighPeakSum += Peaks->AtInterior(X, Y); ++HighCount; }
				else if (E < 0.35f) { LowPeakSum += Peaks->AtInterior(X, Y); ++LowCount; }
			}
		}
		if (HighCount > 0 && LowCount > 0)
		{
			Test.TestTrue(TEXT("Peaks favor high terrain"), HighPeakSum / HighCount > LowPeakSum / LowCount);
		}
	}

	const FGaeaTerrainEvaluationResult SurfaceResult = FGaeaTerrainEvaluator::Evaluate(MakeRockSoilRecipe(), Context);
	float RockTotal = 0.0f;
	float SoilTotal = 0.0f;
	if (!ValidateNormalizedField(*this, SurfaceResult, GaeaTerrainFieldNames::RockMap, RockTotal)) return false;
	if (!ValidateNormalizedField(*this, SurfaceResult, GaeaTerrainFieldNames::Soil, SoilTotal)) return false;

	const FGaeaScalarField* Rock = SurfaceResult.Dataset.FindScalarField(GaeaTerrainFieldNames::RockMap);
	const FGaeaScalarField* Soil = SurfaceResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Soil);
	const FGaeaScalarField* Slope = SurfaceResult.Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
	if (Rock && Soil && Slope)
	{
		float SteepRock = 0.0f;
		float GentleRock = 0.0f;
		float SteepSoil = 0.0f;
		float GentleSoil = 0.0f;
		int32 SteepCount = 0;
		int32 GentleCount = 0;
		for (int32 Y = 0; Y < Slope->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Slope->Domain.Dimensions.X; ++X)
			{
				const float S = Slope->AtInterior(X, Y);
				if (S > 28.0f)
				{
					SteepRock += Rock->AtInterior(X, Y);
					SteepSoil += Soil->AtInterior(X, Y);
					++SteepCount;
				}
				else if (S < 12.0f)
				{
					GentleRock += Rock->AtInterior(X, Y);
					GentleSoil += Soil->AtInterior(X, Y);
					++GentleCount;
				}
			}
		}
		if (SteepCount > 0 && GentleCount > 0)
		{
			Test.TestTrue(TEXT("RockMap favors steeper exposed terrain"), SteepRock / SteepCount > GentleRock / GentleCount);
			Test.TestTrue(TEXT("Soil favors gentler stable terrain"), GentleSoil / GentleCount > SteepSoil / SteepCount);
		}
	}

	VerifyInvalidation(*this, PeaksResult, { GaeaTerrainFieldNames::Peaks });
	VerifyInvalidation(*this, SurfaceResult, { GaeaTerrainFieldNames::RockMap, GaeaTerrainFieldNames::Soil });
	return true;
}

#endif
