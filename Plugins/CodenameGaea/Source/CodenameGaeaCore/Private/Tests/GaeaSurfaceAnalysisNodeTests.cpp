#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FGaeaScalarField MakeSurfaceAnalysisHeight(const FGaeaGridDomain& Domain)
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
				const float Ridge = 0.50f * FMath::Exp(-FMath::Square((NX - 0.22f) / 0.10f));
				const float BowlRadius = FMath::Sqrt(FMath::Square((NX - 0.67f) / 0.22f) + FMath::Square((NY - 0.52f) / 0.25f));
				const float Bowl = -0.34f * FMath::Clamp(1.0f - BowlRadius, 0.0f, 1.0f);
				const float RegionalSlope = 0.18f * (1.0f - NY);
				Height.AtInterior(X, Y) = FMath::Clamp(0.05f + Ridge + Bowl + RegionalSlope, -0.8f, 0.95f);
			}
		}
		return Height;
	}

	FGaeaTerrainEvaluationContext MakeSurfaceAnalysisContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(10000.0, 10000.0, 2200.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(49, 49),
			FVector2d(-500000.0, -500000.0),
			FVector2d(500000.0, 500000.0));
		Context.SourceDataset.SetScalarField(MakeSurfaceAnalysisHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeSingleNodeRecipe(FName Type)
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(5101, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;
		FGaeaTerrainNode Derive;
		Derive.Id = FGuid(5102, 2, 2, GetTypeHash(Type));
		Derive.Type = Type;
		if (Type == GaeaTerrainNodeTypes::Normals)
		{
			Derive.NameParameters.Add(TEXT("Type"), TEXT("Standard"));
			Derive.IntegerParameters.Add(TEXT("Width"), 2048);
			Derive.IntegerParameters.Add(TEXT("Height"), 2048);
			Derive.NumericParameters.Add(TEXT("DetailSize"), 1.0);
			Derive.NameParameters.Add(TEXT("Handedness"), TEXT("Left"));
			Derive.NameParameters.Add(TEXT("UpAxis"), TEXT("ZUp"));
		}
		else if (Type == GaeaTerrainNodeTypes::Occlusion)
		{
			Derive.NumericParameters.Add(TEXT("Strength"), 1.35);
			Derive.IntegerParameters.Add(TEXT("Octaves"), 5);
		}

		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Derive.Id;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Nodes = { Source, Derive };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = Derive.Id;
		return Recipe;
	}

	bool ValidateNormalized(FAutomationTestBase& Test, const FGaeaScalarField* Field, const TCHAR* Label)
	{
		Test.TestNotNull(Label, Field);
		if (!Field) return false;
		float Minimum = 1.0f;
		float Maximum = 0.0f;
		for (int32 Y = 0; Y < Field->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field->Domain.Dimensions.X; ++X)
			{
				const float Value = Field->AtInterior(X, Y);
				Test.TestTrue(TEXT("Surface analysis values remain finite"), FMath::IsFinite(Value));
				Test.TestTrue(TEXT("Surface analysis values remain normalized"), Value >= 0.0f && Value <= 1.0f);
				Minimum = FMath::Min(Minimum, Value);
				Maximum = FMath::Max(Maximum, Value);
			}
		}
		Test.TestTrue(TEXT("Surface analysis field contains variation"), Maximum - Minimum > 0.001f);
		return true;
	}

	void VerifyInvalidation(FAutomationTestBase& Test, const FGaeaTerrainEvaluationResult& Result, std::initializer_list<FName> Names)
	{
		FGaeaTerrainDataset Dataset = Result.Dataset;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		Test.TestNotNull(TEXT("Surface analysis result retains Height"), Height);
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
	FGaeaSurfaceAnalysisDeriveTest,
	"CodenameGaea.Core.Graph.SurfaceAnalysisDerive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSurfaceAnalysisDeriveTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor NormalsDescriptor;
	FGaeaTerrainNodeDescriptor OcclusionDescriptor;
	TestTrue(TEXT("Normals descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Normals, NormalsDescriptor));
	TestTrue(TEXT("Occlusion descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Occlusion, OcclusionDescriptor));
	TestEqual(TEXT("Normals public parameter contract"), NormalsDescriptor.Parameters.Num(), 6);
	TestEqual(TEXT("Occlusion public parameter contract"), OcclusionDescriptor.Parameters.Num(), 2);

	const FGaeaTerrainEvaluationContext Context = MakeSurfaceAnalysisContext();
	const FGaeaTerrainEvaluationResult NormalsResult = FGaeaTerrainEvaluator::Evaluate(MakeSingleNodeRecipe(GaeaTerrainNodeTypes::Normals), Context);
	TestTrue(TEXT("Normals graph evaluates"), NormalsResult.bSuccess);
	if (!NormalsResult.bSuccess)
	{
		AddError(NormalsResult.Error);
		return false;
	}

	const FGaeaScalarField* NormalX = NormalsResult.Dataset.FindScalarField(GaeaTerrainFieldNames::NormalX);
	const FGaeaScalarField* NormalY = NormalsResult.Dataset.FindScalarField(GaeaTerrainFieldNames::NormalY);
	const FGaeaScalarField* NormalZ = NormalsResult.Dataset.FindScalarField(GaeaTerrainFieldNames::NormalZ);
	if (!ValidateNormalized(*this, NormalX, TEXT("NormalX exists"))) return false;
	if (!ValidateNormalized(*this, NormalY, TEXT("NormalY exists"))) return false;
	TestNotNull(TEXT("NormalZ exists"), NormalZ);
	if (!NormalZ) return false;

	float MinimumZ = 1.0f;
	float MaximumZ = 0.0f;
	for (int32 Y = 0; Y < NormalZ->Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < NormalZ->Domain.Dimensions.X; ++X)
		{
			const float Value = NormalZ->AtInterior(X, Y);
			TestTrue(TEXT("NormalZ remains finite"), FMath::IsFinite(Value));
			TestTrue(TEXT("NormalZ remains normalized"), Value >= 0.0f && Value <= 1.0f);
			MinimumZ = FMath::Min(MinimumZ, Value);
			MaximumZ = FMath::Max(MaximumZ, Value);
		}
	}
	TestTrue(TEXT("NormalZ responds to terrain slope"), MaximumZ - MinimumZ > 0.001f);
	TestTrue(TEXT("Up-facing terrain normal keeps positive Z"), MinimumZ >= 0.5f);

	const FGaeaTerrainEvaluationResult OcclusionResult = FGaeaTerrainEvaluator::Evaluate(MakeSingleNodeRecipe(GaeaTerrainNodeTypes::Occlusion), Context);
	TestTrue(TEXT("Occlusion graph evaluates"), OcclusionResult.bSuccess);
	if (!OcclusionResult.bSuccess)
	{
		AddError(OcclusionResult.Error);
		return false;
	}

	const FGaeaScalarField* Occlusion = OcclusionResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Occlusion);
	if (!ValidateNormalized(*this, Occlusion, TEXT("Occlusion exists"))) return false;

	const int32 BowlX = FMath::RoundToInt(0.67f * static_cast<float>(Occlusion->Domain.Dimensions.X - 1));
	const int32 BowlY = FMath::RoundToInt(0.52f * static_cast<float>(Occlusion->Domain.Dimensions.Y - 1));
	const int32 RidgeX = FMath::RoundToInt(0.22f * static_cast<float>(Occlusion->Domain.Dimensions.X - 1));
	const int32 RidgeY = BowlY;
	TestTrue(TEXT("Recessed bowl is more occluded than exposed ridge"), Occlusion->AtInterior(BowlX, BowlY) > Occlusion->AtInterior(RidgeX, RidgeY));

	VerifyInvalidation(*this, NormalsResult, { GaeaTerrainFieldNames::NormalX, GaeaTerrainFieldNames::NormalY, GaeaTerrainFieldNames::NormalZ });
	VerifyInvalidation(*this, OcclusionResult, { GaeaTerrainFieldNames::Occlusion });
	return true;
}

#endif
