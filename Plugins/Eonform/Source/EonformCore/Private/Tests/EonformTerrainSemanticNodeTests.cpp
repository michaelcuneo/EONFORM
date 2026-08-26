#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainSemanticNodes.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformScalarField MakeSemanticHeight(const FEonformGridDomain& Domain)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Height;
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

	FEonformTerrainEvaluationContext MakeSemanticContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(8000.0, 8000.0, 1800.0, 0.0);
		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(41, 41),
			FVector2d(-400000.0, -400000.0),
			FVector2d(400000.0, 400000.0));
		Context.SourceDataset.SetScalarField(MakeSemanticHeight(Domain));
		return Context;
	}

	FEonformTerrainRecipe MakeSingleSemanticRecipe(FName Type)
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source;
		Source.Id = FGuid(4101, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;
		FEonformTerrainNode Semantic;
		Semantic.Id = FGuid(4102, 2, 2, GetTypeHash(Type));
		Semantic.Type = Type;
		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Semantic.Id;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Nodes = { Source, Semantic };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = Semantic.Id;
		return Recipe;
	}

	FEonformTerrainRecipe MakeRockSoilRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source;
		Source.Id = FGuid(4201, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;
		FEonformTerrainNode Rock;
		Rock.Id = FGuid(4202, 2, 2, 2);
		Rock.Type = EonformTerrainNodeTypes::RockMap;
		Rock.NumericParameters.Add(TEXT("Exposure"), 0.72);
		Rock.NumericParameters.Add(TEXT("Steepness"), 0.68);
		FEonformTerrainNode Soil;
		Soil.Id = FGuid(4203, 3, 3, 3);
		Soil.Type = EonformTerrainNodeTypes::Soil;
		Soil.NumericParameters.Add(TEXT("Coverage"), 0.82);
		Soil.NumericParameters.Add(TEXT("ValleyBias"), 0.76);

		FEonformTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Terrain");
		A.ToNode = Rock.Id;
		A.ToInput = TEXT("Terrain");
		FEonformTerrainConnection B;
		B.FromNode = Rock.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Soil.Id;
		B.ToInput = TEXT("Terrain");
		Recipe.Nodes = { Source, Rock, Soil };
		Recipe.Connections = { A, B };
		Recipe.OutputNode = Soil.Id;
		return Recipe;
	}

	bool ValidateNormalizedField(FAutomationTestBase& Test, const FEonformTerrainEvaluationResult& Result, FName FieldName, float& OutTotal)
	{
		Test.TestTrue(TEXT("Semantic graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}
		const FEonformScalarField* Field = Result.Dataset.FindScalarField(FieldName);
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

	void VerifyInvalidation(FAutomationTestBase& Test, const FEonformTerrainEvaluationResult& Result, std::initializer_list<FName> Names)
	{
		FEonformTerrainDataset Dataset = Result.Dataset;
		const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height) return;
		FEonformScalarField Replacement = *Height;
		Replacement.AtInterior(Replacement.Domain.Dimensions.X / 2, Replacement.Domain.Dimensions.Y / 2) += 0.002f;
		Test.TestTrue(TEXT("Replacement Height publishes"), Dataset.SetScalarField(MoveTemp(Replacement)));
		for (const FName Name : Names)
		{
			Test.TestFalse(*FString::Printf(TEXT("%s invalidates with Height"), *Name.ToString()), Dataset.HasScalarField(Name));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainSemanticDeriveTest,
	"Eonform.Core.Graph.TerrainSemanticDerive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainSemanticDeriveTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor PeaksDescriptor;
	FEonformTerrainNodeDescriptor RockDescriptor;
	FEonformTerrainNodeDescriptor SoilDescriptor;
	TestTrue(TEXT("Peaks descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Peaks, PeaksDescriptor));
	TestTrue(TEXT("RockMap descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::RockMap, RockDescriptor));
	TestTrue(TEXT("Soil descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Soil, SoilDescriptor));
	TestEqual(TEXT("Peaks parameter contract"), PeaksDescriptor.Parameters.Num(), 2);
	TestEqual(TEXT("RockMap parameter contract"), RockDescriptor.Parameters.Num(), 2);
	TestEqual(TEXT("Soil parameter contract"), SoilDescriptor.Parameters.Num(), 2);

	const FEonformTerrainEvaluationContext Context = MakeSemanticContext();
	const FEonformTerrainEvaluationResult PeaksResult = FEonformTerrainEvaluator::Evaluate(MakeSingleSemanticRecipe(EonformTerrainNodeTypes::Peaks), Context);
	float PeaksTotal = 0.0f;
	if (!ValidateNormalizedField(*this, PeaksResult, EonformTerrainFieldNames::Peaks, PeaksTotal)) return false;

	const FEonformScalarField* Peaks = PeaksResult.Dataset.FindScalarField(EonformTerrainFieldNames::Peaks);
	const FEonformScalarField* Elevation = PeaksResult.Dataset.FindScalarField(EonformTerrainFieldNames::Elevation);
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
			TestTrue(TEXT("Peaks favor high terrain"), HighPeakSum / HighCount > LowPeakSum / LowCount);
		}
	}

	const FEonformTerrainEvaluationResult SurfaceResult = FEonformTerrainEvaluator::Evaluate(MakeRockSoilRecipe(), Context);
	float RockTotal = 0.0f;
	float SoilTotal = 0.0f;
	if (!ValidateNormalizedField(*this, SurfaceResult, EonformTerrainFieldNames::RockMap, RockTotal)) return false;
	if (!ValidateNormalizedField(*this, SurfaceResult, EonformTerrainFieldNames::Soil, SoilTotal)) return false;

	const FEonformScalarField* Rock = SurfaceResult.Dataset.FindScalarField(EonformTerrainFieldNames::RockMap);
	const FEonformScalarField* Soil = SurfaceResult.Dataset.FindScalarField(EonformTerrainFieldNames::Soil);
	const FEonformScalarField* Slope = SurfaceResult.Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
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
			TestTrue(TEXT("RockMap favors steeper exposed terrain"), SteepRock / SteepCount > GentleRock / GentleCount);
			TestTrue(TEXT("Soil favors gentler stable terrain"), GentleSoil / GentleCount > SteepSoil / SteepCount);
		}
	}

	VerifyInvalidation(*this, PeaksResult, { EonformTerrainFieldNames::Peaks });
	VerifyInvalidation(*this, SurfaceResult, { EonformTerrainFieldNames::RockMap, EonformTerrainFieldNames::Soil });
	return true;
}

#endif
