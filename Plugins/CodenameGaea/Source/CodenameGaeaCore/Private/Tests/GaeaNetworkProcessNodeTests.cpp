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
	FGaeaScalarField MakeNetworkHeight(const FGaeaGridDomain& Domain)
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
				const float NX = static_cast<float>(X - Domain.Dimensions.X / 2) / static_cast<float>(Domain.Dimensions.X);
				const float Downstream = 0.78f - 0.035f * static_cast<float>(Y);
				const float Valley = 0.12f * FMath::Abs(NX);
				Height.AtInterior(X, Y) = FMath::Clamp(Downstream + Valley, 0.08f, 0.92f);
			}
		}
		return Height;
	}

	FGaeaTerrainEvaluationContext MakeNetworkContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(1600.0, 1600.0, 1200.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(17, 17),
			FVector2d(-80000.0, -80000.0),
			FVector2d(80000.0, 80000.0));
		Context.SourceDataset.SetScalarField(MakeNetworkHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeNetworkRecipe()
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(901, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode Anastomosis;
		Anastomosis.Id = FGuid(902, 2, 2, 2);
		Anastomosis.Type = GaeaTerrainNodeTypes::Anastomosis;
		Anastomosis.NumericParameters.Add(TEXT("NetworkThreshold"), 0.05);
		Anastomosis.NumericParameters.Add(TEXT("FloodplainSlopeDegrees"), 35.0);
		Anastomosis.NumericParameters.Add(TEXT("Braiding"), 0.8);
		Anastomosis.NumericParameters.Add(TEXT("Reconnection"), 0.8);
		Anastomosis.IntegerParameters.Add(TEXT("WidthCells"), 2);
		Anastomosis.BoolParameters.Add(TEXT("AffectHeight"), false);

		FGaeaTerrainNode Lichtenberg;
		Lichtenberg.Id = FGuid(903, 3, 3, 3);
		Lichtenberg.Type = GaeaTerrainNodeTypes::Lichtenberg;
		Lichtenberg.IntegerParameters.Add(TEXT("Seeds"), 5);
		Lichtenberg.IntegerParameters.Add(TEXT("Steps"), 32);
		Lichtenberg.NumericParameters.Add(TEXT("BranchChance"), 0.3);
		Lichtenberg.NumericParameters.Add(TEXT("TerrainGuidance"), 0.7);
		Lichtenberg.NumericParameters.Add(TEXT("Persistence"), 0.93);
		Lichtenberg.BoolParameters.Add(TEXT("AffectHeight"), false);

		FGaeaTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Terrain");
		A.ToNode = Anastomosis.Id;
		A.ToInput = TEXT("Terrain");

		FGaeaTerrainConnection B;
		B.FromNode = Anastomosis.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Lichtenberg.Id;
		B.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Anastomosis, Lichtenberg };
		Recipe.Connections = { A, B };
		Recipe.OutputNode = Lichtenberg.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaNetworkProcessChainTest,
	"CodenameGaea.Core.Graph.NetworkProcessChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaNetworkProcessChainTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor AnastomosisDescriptor;
	FGaeaTerrainNodeDescriptor LichtenbergDescriptor;
	TestTrue(TEXT("Anastomosis descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Anastomosis, AnastomosisDescriptor));
	TestTrue(TEXT("Lichtenberg descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Lichtenberg, LichtenbergDescriptor));
	TestEqual(TEXT("Anastomosis parameter contract"), AnastomosisDescriptor.Parameters.Num(), 8);
	TestEqual(TEXT("Lichtenberg parameter contract"), LichtenbergDescriptor.Parameters.Num(), 8);

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeNetworkRecipe(), MakeNetworkContext());
	TestTrue(TEXT("Network process graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Anastomosis = Result.Dataset.FindScalarField(TEXT("Anastomosis"));
	const FGaeaScalarField* Reconnection = Result.Dataset.FindScalarField(TEXT("AnastomosisReconnection"));
	const FGaeaScalarField* Lichtenberg = Result.Dataset.FindScalarField(TEXT("Lichtenberg"));
	const FGaeaScalarField* BranchOrder = Result.Dataset.FindScalarField(TEXT("LichtenbergBranchOrder"));
	TestNotNull(TEXT("Anastomosis field survives non-destructive chain"), Anastomosis);
	TestNotNull(TEXT("Anastomosis reconnection field is published"), Reconnection);
	TestNotNull(TEXT("Lichtenberg pattern is published"), Lichtenberg);
	TestNotNull(TEXT("Lichtenberg branch order is published"), BranchOrder);
	if (!Anastomosis || !Reconnection || !Lichtenberg || !BranchOrder) return false;

	// Anastomosis consumes physical catchment, so the chain should stop at the
	// flow-analysis tier. Lichtenberg itself consumes only terrain context and
	// must not silently upgrade the dataset to the full hydrology network.
	TestNotNull(TEXT("Anastomosis derives FlowDirection on demand"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::FlowDirection));
	TestNotNull(TEXT("Anastomosis derives FlowAccumulation on demand"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation));
	TestNotNull(TEXT("Anastomosis derives CatchmentAreaKm2 on demand"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2));
	TestNull(TEXT("Network process chain does not derive DistanceToOutletKm"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::DistanceToOutletKm));
	TestNull(TEXT("Network process chain does not derive StreamOrder"), Result.Dataset.FindScalarField(GaeaTerrainFieldNames::StreamOrder));

	float MaxAnastomosis = 0.0f;
	float MaxReconnection = 0.0f;
	float MaxLichtenberg = 0.0f;
	float MaxBranchOrder = 0.0f;
	for (const float Value : Anastomosis->Values) MaxAnastomosis = FMath::Max(MaxAnastomosis, Value);
	for (const float Value : Reconnection->Values) MaxReconnection = FMath::Max(MaxReconnection, Value);
	for (const float Value : Lichtenberg->Values) MaxLichtenberg = FMath::Max(MaxLichtenberg, Value);
	for (const float Value : BranchOrder->Values) MaxBranchOrder = FMath::Max(MaxBranchOrder, Value);

	TestTrue(TEXT("Floodplain network produces channels"), MaxAnastomosis > 0.0f);
	TestTrue(TEXT("Floodplain network produces reconnecting structure"), MaxReconnection > 0.0f);
	TestTrue(TEXT("Lichtenberg growth produces a branching pattern"), MaxLichtenberg > 0.0f);
	TestTrue(TEXT("Lichtenberg records branch order"), MaxBranchOrder >= 1.0f);
	return true;
}

#endif
