#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaReferenceFidelityExtendedNodes.h"
#include "GaeaReferenceFidelityNodes.h"
#include "GaeaReferenceFidelityProcessNodes.h"
#include "GaeaReferenceFidelityTransposeNode.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	void RegisterAuditedDependencies()
	{
		RegisterGaeaReferenceFidelityNodes();
		RegisterGaeaReferenceFidelityExtendedNodes();
		RegisterGaeaReferenceFidelityTransposeNode();
		RegisterGaeaReferenceFidelityProcessNodes();
	}

	FGaeaTerrainEvaluationContext Context(int32 Resolution = 65)
	{
		FGaeaTerrainEvaluationContext C;
		C.TargetResolution = FIntPoint(Resolution, Resolution);
		C.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(10000.0, 10000.0, 3000.0, 0.0);
		return C;
	}

	double MeanAbsoluteDifference(const FGaeaScalarField& A, const FGaeaScalarField& B)
	{
		if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain) return 0.0;
		double Sum = 0.0;
		for (int32 Y = 0; Y < A.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < A.Domain.Dimensions.X; ++X)
			{
				Sum += FMath::Abs(static_cast<double>(A.AtInterior(X, Y) - B.AtInterior(X, Y)));
			}
		}
		return Sum / static_cast<double>(A.Domain.GetInteriorSampleCount());
	}

	FGaeaTerrainNode Radial(FGuid Id)
	{
		FGaeaTerrainNode N; N.Id = Id; N.Type = GaeaTerrainNodeTypes::RadialGradient;
		N.NumericParameters.Add(TEXT("Scale"), 0.82);
		N.NumericParameters.Add(TEXT("Height"), 0.92);
		return N;
	}

	FGaeaTerrainConnection Link(const FGuid& From, FName Output, const FGuid& To, FName Input)
	{
		FGaeaTerrainConnection C; C.FromNode = From; C.FromOutput = Output; C.ToNode = To; C.ToInput = Input; return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaReferenceSurfaceCompositionTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.MountainSurfaceComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaReferenceSurfaceCompositionTest::RunTest(const FString& Parameters)
{
	RegisterAuditedDependencies();

	FGaeaTerrainNode Base = Radial(FGuid(1, 1, 1, 1));
	FGaeaTerrainNode Warp;
	Warp.Id = FGuid(2, 2, 2, 2); Warp.Type = GaeaTerrainNodeTypes::Warp;
	Warp.NumericParameters.Add(TEXT("Size"), 0.42);
	Warp.NumericParameters.Add(TEXT("Strength"), 0.28);
	Warp.NameParameters.Add(TEXT("WarpSource"), TEXT("Voronoi R"));
	Warp.IntegerParameters.Add(TEXT("Complexity"), 4);
	Warp.IntegerParameters.Add(TEXT("Seed"), 741);

	FGaeaTerrainRecipe WarpedRecipe;
	WarpedRecipe.Nodes = { Base, Warp };
	WarpedRecipe.Connections.Add(Link(Base.Id, TEXT("Out"), Warp.Id, TEXT("Input")));
	WarpedRecipe.OutputNode = Warp.Id;
	const FGaeaTerrainEvaluationResult Warped = FGaeaTerrainEvaluator::Evaluate(WarpedRecipe, Context());
	TestTrue(TEXT("Audited Warp evaluates"), Warped.bSuccess);

	FGaeaTerrainRecipe BaseRecipe; BaseRecipe.Nodes = { Base }; BaseRecipe.OutputNode = Base.Id;
	const FGaeaTerrainEvaluationResult Unwarped = FGaeaTerrainEvaluator::Evaluate(BaseRecipe, Context());
	const FGaeaScalarField* WH = Warped.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* UH = Unwarped.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (WH && UH) TestTrue(TEXT("Warp materially deforms terrain"), MeanAbsoluteDifference(*WH, *UH) > 0.002);

	FGaeaTerrainNode Rock;
	Rock.Id = FGuid(3, 3, 3, 3); Rock.Type = GaeaTerrainNodeTypes::RockNoise;
	Rock.NumericParameters.Add(TEXT("Size"), 0.65);
	Rock.NumericParameters.Add(TEXT("Variety"), 0.72);
	Rock.IntegerParameters.Add(TEXT("Octaves"), 4);
	Rock.IntegerParameters.Add(TEXT("Seed"), 991);
	Rock.NameParameters.Add(TEXT("Style"), TEXT("C"));
	FGaeaTerrainRecipe RockRecipe; RockRecipe.Nodes = { Rock }; RockRecipe.OutputNode = Rock.Id;
	const FGaeaTerrainEvaluationResult RockResult = FGaeaTerrainEvaluator::Evaluate(RockRecipe, Context());
	TestTrue(TEXT("RockNoise evaluates as a source"), RockResult.bSuccess);
	const FGaeaScalarField* RockHeight = RockResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (RockHeight)
	{
		float Min = TNumericLimits<float>::Max(), Max = TNumericLimits<float>::Lowest();
		for (const float V : RockHeight->Values) { Min = FMath::Min(Min, V); Max = FMath::Max(Max, V); }
		TestTrue(TEXT("RockNoise produces non-flat rock relief"), Max - Min > 0.20f);
	}

	FGaeaTerrainNode Transpose;
	Transpose.Id = FGuid(4, 4, 4, 4); Transpose.Type = GaeaTerrainNodeTypes::Transpose;
	Transpose.NameParameters.Add(TEXT("Mode"), TEXT("Insert"));
	Transpose.NumericParameters.Add(TEXT("Amount"), 0.45);
	Transpose.NumericParameters.Add(TEXT("Threshold"), 0.18);
	Transpose.BoolParameters.Add(TEXT("Flatten"), true);
	FGaeaTerrainRecipe InsertRecipe;
	InsertRecipe.Nodes = { Base, Rock, Transpose };
	InsertRecipe.Connections.Add(Link(Base.Id, TEXT("Out"), Transpose.Id, TEXT("Input")));
	InsertRecipe.Connections.Add(Link(Rock.Id, TEXT("Out"), Transpose.Id, TEXT("Reference")));
	InsertRecipe.OutputNode = Transpose.Id;
	const FGaeaTerrainEvaluationResult Inserted = FGaeaTerrainEvaluator::Evaluate(InsertRecipe, Context());
	TestTrue(TEXT("Transpose Insert evaluates with RockNoise reference"), Inserted.bSuccess);
	const FGaeaScalarField* IH = Inserted.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (IH && UH) TestTrue(TEXT("Transpose Insert embeds rock relief"), MeanAbsoluteDifference(*IH, *UH) > 0.002);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaReferenceErosionScaleTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.ErosionScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaReferenceErosionScaleTest::RunTest(const FString& Parameters)
{
	RegisterAuditedDependencies();

	auto EvaluateScale = [](double Scale)
	{
		FGaeaTerrainNode Base = Radial(FGuid(10, 10, 10, static_cast<uint32>(Scale)));
		FGaeaTerrainNode Erosion;
		Erosion.Id = FGuid(11, 11, 11, static_cast<uint32>(Scale));
		Erosion.Type = GaeaTerrainNodeTypes::Erosion2;
		Erosion.IntegerParameters.Add(TEXT("Duration"), 10);
		Erosion.NumericParameters.Add(TEXT("Downcutting"), 0.48);
		Erosion.NumericParameters.Add(TEXT("ErosionScale"), Scale);
		Erosion.NumericParameters.Add(TEXT("Shape"), 0.55);
		Erosion.NumericParameters.Add(TEXT("ShapeSharpness"), 0.45);
		Erosion.IntegerParameters.Add(TEXT("Seed"), 1193);
		FGaeaTerrainRecipe Recipe;
		Recipe.Nodes = { Base, Erosion };
		Recipe.Connections.Add(Link(Base.Id, TEXT("Out"), Erosion.Id, TEXT("Terrain")));
		Recipe.OutputNode = Erosion.Id;
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context());
	};

	const FGaeaTerrainEvaluationResult Fine = EvaluateScale(100.0);
	const FGaeaTerrainEvaluationResult Broad = EvaluateScale(4000.0);
	TestTrue(TEXT("Fine Erosion2 evaluates"), Fine.bSuccess);
	TestTrue(TEXT("Broad Erosion2 evaluates"), Broad.bSuccess);
	const FGaeaScalarField* FineHeight = Fine.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* BroadHeight = Broad.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* FineWear = Fine.Dataset.FindScalarField(GaeaTerrainFieldNames::Wear);
	const FGaeaScalarField* FineFlow = Fine.Dataset.FindScalarField(GaeaTerrainFieldNames::Flow);
	TestNotNull(TEXT("Erosion2 publishes Wear"), FineWear);
	TestNotNull(TEXT("Erosion2 publishes Flow"), FineFlow);
	if (FineHeight && BroadHeight) TestTrue(TEXT("Physical erosion scale materially changes terrain structure"), MeanAbsoluteDifference(*FineHeight, *BroadHeight) > 0.0002);
	if (FineWear)
	{
		float MaxWear = 0.0f; for (const float V : FineWear->Values) MaxWear = FMath::Max(MaxWear, V);
		TestTrue(TEXT("Erosion2 produces non-zero hydraulic wear"), MaxWear > 0.0f);
	}
	if (FineFlow)
	{
		float MaxFlow = 0.0f; for (const float V : FineFlow->Values) MaxFlow = FMath::Max(MaxFlow, V);
		TestTrue(TEXT("Erosion2 produces concentrated flow"), MaxFlow > 0.0f);
	}
	return true;
}

#endif
