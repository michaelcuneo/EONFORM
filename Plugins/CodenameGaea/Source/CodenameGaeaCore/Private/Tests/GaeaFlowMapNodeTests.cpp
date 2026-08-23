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
	FGaeaScalarField MakeFlowMapHeight(const FGaeaGridDomain& Domain)
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
				const float Downhill = 0.72f * (1.0f - NY);
				const float ValleyWalls = 0.30f * FMath::Pow(FMath::Abs(NX - 0.5f) * 2.0f, 1.35f);
				const float TributaryRelief = 0.035f * FMath::Sin(NY * UE_TWO_PI * 2.0f) * FMath::Abs(NX - 0.5f) * 2.0f;
				Height.AtInterior(X, Y) = FMath::Clamp(-0.18f + Downhill + ValleyWalls + TributaryRelief, -0.9f, 0.95f);
			}
		}
		return Height;
	}

	FGaeaTerrainEvaluationContext MakeFlowMapContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(6400.0, 6400.0, 1600.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(33, 33),
			FVector2d(-320000.0, -320000.0),
			FVector2d(320000.0, 320000.0));
		Context.SourceDataset.SetScalarField(MakeFlowMapHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeFlowMapRecipe(bool bClassic)
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(3001, 1, 1, bClassic ? 11 : 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode FlowNode;
		FlowNode.Id = FGuid(3002, 2, 2, bClassic ? 22 : 2);
		FlowNode.Type = bClassic ? GaeaTerrainNodeTypes::FlowMapClassic : GaeaTerrainNodeTypes::FlowMap;
		if (bClassic)
		{
			FlowNode.NumericParameters.Add(TEXT("Rainfall"), 0.78);
			FlowNode.NumericParameters.Add(TEXT("Primary"), 1.0);
			FlowNode.NumericParameters.Add(TEXT("Secondary"), 0.78);
			FlowNode.NumericParameters.Add(TEXT("Tertiary"), 0.52);
			FlowNode.NumericParameters.Add(TEXT("Quaternary"), 0.28);
			FlowNode.BoolParameters.Add(TEXT("Simulate2X"), true);
			FlowNode.NumericParameters.Add(TEXT("Enhance"), 0.45);
			FlowNode.NameParameters.Add(TEXT("Quality"), TEXT("Full"));
		}
		else
		{
			FlowNode.NumericParameters.Add(TEXT("FlowLength"), 0.82);
			FlowNode.NumericParameters.Add(TEXT("FlowVolume"), 0.68);
			FlowNode.IntegerParameters.Add(TEXT("Seed"), 7331);
		}

		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = FlowNode.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, FlowNode };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = FlowNode.Id;
		return Recipe;
	}

	bool ValidateFlowField(
		FAutomationTestBase& Test,
		const FGaeaTerrainEvaluationResult& Result,
		FName FlowName,
		float& OutMaximum,
		float& OutTotal)
	{
		Test.TestTrue(TEXT("Flow map graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}

		const FGaeaScalarField* Flow = Result.Dataset.FindScalarField(FlowName);
		const FGaeaScalarField* Catchment = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2);
		Test.TestNotNull(TEXT("Flow map is persisted in the terrain dataset"), Flow);
		Test.TestNotNull(TEXT("Physical catchment field is available"), Catchment);
		if (!Flow || !Catchment) return false;

		OutMaximum = 0.0f;
		OutTotal = 0.0f;
		double MaximumCatchment = -1.0;
		int32 MaximumCatchmentX = 0;
		int32 MaximumCatchmentY = 0;
		for (int32 Y = 0; Y < Flow->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Flow->Domain.Dimensions.X; ++X)
			{
				const float Value = Flow->AtInterior(X, Y);
				Test.TestTrue(TEXT("Flow map values remain finite"), FMath::IsFinite(Value));
				Test.TestTrue(TEXT("Flow map values remain normalized"), Value >= 0.0f && Value <= 1.0f);
				OutMaximum = FMath::Max(OutMaximum, Value);
				OutTotal += Value;

				const double Area = static_cast<double>(Catchment->AtInterior(X, Y));
				if (Area > MaximumCatchment)
				{
					MaximumCatchment = Area;
					MaximumCatchmentX = X;
					MaximumCatchmentY = Y;
				}
			}
		}

		Test.TestTrue(TEXT("Flow map contains meaningful routed drainage"), OutMaximum > 0.05f);
		Test.TestTrue(TEXT("Flow map contains non-zero coverage"), OutTotal > 0.5f);
		Test.TestTrue(TEXT("Largest physical catchment resolves to a strong flow channel"),
			Flow->AtInterior(MaximumCatchmentX, MaximumCatchmentY) > 0.05f);
		return true;
	}

	bool VerifyHeightInvalidation(
		FAutomationTestBase& Test,
		const FGaeaTerrainEvaluationResult& Result,
		std::initializer_list<FName> DerivedNames)
	{
		FGaeaTerrainDataset Dataset = Result.Dataset;
		const FGaeaScalarField* ExistingHeight = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Test.TestNotNull(TEXT("Result retains Height"), ExistingHeight) || !ExistingHeight) return false;

		FGaeaScalarField Replacement = *ExistingHeight;
		Replacement.AtInterior(Replacement.Domain.Dimensions.X / 2, Replacement.Domain.Dimensions.Y / 2) += 0.001f;
		if (!Test.TestTrue(TEXT("Replacement Height publishes"), Dataset.SetScalarField(MoveTemp(Replacement)))) return false;

		for (const FName Name : DerivedNames)
		{
			Test.TestFalse(*FString::Printf(TEXT("%s invalidates after Height changes"), *Name.ToString()), Dataset.HasScalarField(Name));
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaFlowMapDeriveTest,
	"CodenameGaea.Core.Graph.FlowMapDerive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaFlowMapDeriveTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor ModernDescriptor;
	FGaeaTerrainNodeDescriptor ClassicDescriptor;
	TestTrue(TEXT("FlowMap descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::FlowMap, ModernDescriptor));
	TestTrue(TEXT("FlowMapClassic descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::FlowMapClassic, ClassicDescriptor));
	TestEqual(TEXT("FlowMap public parameter contract"), ModernDescriptor.Parameters.Num(), 3);
	TestEqual(TEXT("FlowMapClassic public parameter contract"), ClassicDescriptor.Parameters.Num(), 8);

	const FGaeaTerrainEvaluationContext Context = MakeFlowMapContext();
	const FGaeaTerrainEvaluationResult Modern = FGaeaTerrainEvaluator::Evaluate(MakeFlowMapRecipe(false), Context);
	const FGaeaTerrainEvaluationResult Classic = FGaeaTerrainEvaluator::Evaluate(MakeFlowMapRecipe(true), Context);

	float ModernMaximum = 0.0f;
	float ModernTotal = 0.0f;
	float ClassicMaximum = 0.0f;
	float ClassicTotal = 0.0f;
	if (!ValidateFlowField(*this, Modern, TEXT("FlowMap"), ModernMaximum, ModernTotal)) return false;
	if (!ValidateFlowField(*this, Classic, TEXT("FlowMapClassic"), ClassicMaximum, ClassicTotal)) return false;

	const FGaeaScalarField* ModernFlow = Modern.Dataset.FindScalarField(TEXT("FlowMap"));
	const FGaeaScalarField* ClassicFlow = Classic.Dataset.FindScalarField(TEXT("FlowMapClassic"));
	if (!ModernFlow || !ClassicFlow) return false;

	float Difference = 0.0f;
	for (int32 Y = 0; Y < ModernFlow->Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < ModernFlow->Domain.Dimensions.X; ++X)
		{
			Difference += FMath::Abs(ModernFlow->AtInterior(X, Y) - ClassicFlow->AtInterior(X, Y));
		}
	}
	TestTrue(TEXT("Modern and classic flow derivations remain behaviorally distinct"), Difference > 0.1f);

	TestNotNull(TEXT("Modern direction semantic exists"), Modern.Dataset.FindScalarField(TEXT("FlowMapDirection")));
	TestNotNull(TEXT("Modern accumulation semantic exists"), Modern.Dataset.FindScalarField(TEXT("FlowMapAccumulation")));
	TestNotNull(TEXT("Classic hierarchy semantic exists"), Classic.Dataset.FindScalarField(TEXT("FlowMapClassicHierarchy")));

	VerifyHeightInvalidation(*this, Modern, { TEXT("FlowMap"), TEXT("FlowMapDirection"), TEXT("FlowMapAccumulation") });
	VerifyHeightInvalidation(*this, Classic, { TEXT("FlowMapClassic"), TEXT("FlowMapClassicHierarchy") });
	return true;
}

#endif
