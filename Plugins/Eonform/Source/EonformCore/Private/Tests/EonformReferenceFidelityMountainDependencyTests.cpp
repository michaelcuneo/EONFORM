#if WITH_DEV_AUTOMATION_TESTS

#include "EonformReferenceFidelityExtendedNodes.h"
#include "EonformReferenceFidelityNodes.h"
#include "EonformReferenceFidelityProcessNodes.h"
#include "EonformReferenceFidelityTransposeNode.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	void RegisterAuditedDependencies()
	{
		RegisterEonformReferenceFidelityNodes();
		RegisterEonformReferenceFidelityExtendedNodes();
		RegisterEonformReferenceFidelityTransposeNode();
		RegisterEonformReferenceFidelityProcessNodes();
	}

	FEonformTerrainEvaluationContext Context(int32 Resolution = 65)
	{
		FEonformTerrainEvaluationContext C;
		C.TargetResolution = FIntPoint(Resolution, Resolution);
		C.PhysicalMetrics = FEonformTerrainPhysicalMetrics(10000.0, 10000.0, 3000.0, 0.0);
		return C;
	}

	double MeanAbsoluteDifference(const FEonformScalarField& A, const FEonformScalarField& B)
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

	FEonformTerrainNode Radial(FGuid Id)
	{
		FEonformTerrainNode N; N.Id = Id; N.Type = EonformTerrainNodeTypes::RadialGradient;
		N.NumericParameters.Add(TEXT("Scale"), 0.82);
		N.NumericParameters.Add(TEXT("Height"), 0.92);
		return N;
	}

	FEonformTerrainConnection Link(const FGuid& From, FName Output, const FGuid& To, FName Input)
	{
		FEonformTerrainConnection C; C.FromNode = From; C.FromOutput = Output; C.ToNode = To; C.ToInput = Input; return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformReferenceSurfaceCompositionTest,
	"Eonform.Core.Graph.ReferenceFidelity.MountainSurfaceComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformReferenceSurfaceCompositionTest::RunTest(const FString& Parameters)
{
	RegisterAuditedDependencies();

	FEonformTerrainNode Base = Radial(FGuid(1, 1, 1, 1));
	FEonformTerrainNode Warp;
	Warp.Id = FGuid(2, 2, 2, 2); Warp.Type = EonformTerrainNodeTypes::Warp;
	Warp.NumericParameters.Add(TEXT("Size"), 0.42);
	Warp.NumericParameters.Add(TEXT("Strength"), 0.28);
	Warp.NameParameters.Add(TEXT("WarpSource"), TEXT("Voronoi R"));
	Warp.IntegerParameters.Add(TEXT("Complexity"), 4);
	Warp.IntegerParameters.Add(TEXT("Seed"), 741);

	FEonformTerrainRecipe WarpedRecipe;
	WarpedRecipe.Nodes = { Base, Warp };
	WarpedRecipe.Connections.Add(Link(Base.Id, TEXT("Out"), Warp.Id, TEXT("Input")));
	WarpedRecipe.OutputNode = Warp.Id;
	const FEonformTerrainEvaluationResult Warped = FEonformTerrainEvaluator::Evaluate(WarpedRecipe, Context());
	TestTrue(TEXT("Audited Warp evaluates"), Warped.bSuccess);

	FEonformTerrainRecipe BaseRecipe; BaseRecipe.Nodes = { Base }; BaseRecipe.OutputNode = Base.Id;
	const FEonformTerrainEvaluationResult Unwarped = FEonformTerrainEvaluator::Evaluate(BaseRecipe, Context());
	const FEonformScalarField* WH = Warped.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* UH = Unwarped.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (WH && UH) TestTrue(TEXT("Warp materially deforms terrain"), MeanAbsoluteDifference(*WH, *UH) > 0.002);

	FEonformTerrainNode Rock;
	Rock.Id = FGuid(3, 3, 3, 3); Rock.Type = EonformTerrainNodeTypes::RockNoise;
	Rock.NumericParameters.Add(TEXT("Size"), 0.65);
	Rock.NumericParameters.Add(TEXT("Variety"), 0.72);
	Rock.IntegerParameters.Add(TEXT("Octaves"), 4);
	Rock.IntegerParameters.Add(TEXT("Seed"), 991);
	Rock.NameParameters.Add(TEXT("Style"), TEXT("C"));
	FEonformTerrainRecipe RockRecipe; RockRecipe.Nodes = { Rock }; RockRecipe.OutputNode = Rock.Id;
	const FEonformTerrainEvaluationResult RockResult = FEonformTerrainEvaluator::Evaluate(RockRecipe, Context());
	TestTrue(TEXT("RockNoise evaluates as a source"), RockResult.bSuccess);
	const FEonformScalarField* RockHeight = RockResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (RockHeight)
	{
		float Min = TNumericLimits<float>::Max(), Max = TNumericLimits<float>::Lowest();
		for (const float V : RockHeight->Values) { Min = FMath::Min(Min, V); Max = FMath::Max(Max, V); }
		TestTrue(TEXT("RockNoise produces non-flat rock relief"), Max - Min > 0.20f);
	}

	FEonformTerrainNode Transpose;
	Transpose.Id = FGuid(4, 4, 4, 4); Transpose.Type = EonformTerrainNodeTypes::Transpose;
	Transpose.NameParameters.Add(TEXT("Mode"), TEXT("Insert"));
	Transpose.NumericParameters.Add(TEXT("Amount"), 0.45);
	Transpose.NumericParameters.Add(TEXT("Threshold"), 0.18);
	Transpose.BoolParameters.Add(TEXT("Flatten"), true);
	FEonformTerrainRecipe InsertRecipe;
	InsertRecipe.Nodes = { Base, Rock, Transpose };
	InsertRecipe.Connections.Add(Link(Base.Id, TEXT("Out"), Transpose.Id, TEXT("Input")));
	InsertRecipe.Connections.Add(Link(Rock.Id, TEXT("Out"), Transpose.Id, TEXT("Reference")));
	InsertRecipe.OutputNode = Transpose.Id;
	const FEonformTerrainEvaluationResult Inserted = FEonformTerrainEvaluator::Evaluate(InsertRecipe, Context());
	TestTrue(TEXT("Transpose Insert evaluates with RockNoise reference"), Inserted.bSuccess);
	const FEonformScalarField* IH = Inserted.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (IH && UH) TestTrue(TEXT("Transpose Insert embeds rock relief"), MeanAbsoluteDifference(*IH, *UH) > 0.002);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformReferenceErosionScaleTest,
	"Eonform.Core.Graph.ReferenceFidelity.ErosionScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformReferenceErosionScaleTest::RunTest(const FString& Parameters)
{
	RegisterAuditedDependencies();

	auto EvaluateScale = [](double Scale)
	{
		FEonformTerrainNode Base = Radial(FGuid(10, 10, 10, static_cast<uint32>(Scale)));
		FEonformTerrainNode Erosion;
		Erosion.Id = FGuid(11, 11, 11, static_cast<uint32>(Scale));
		Erosion.Type = EonformTerrainNodeTypes::Erosion2;
		Erosion.IntegerParameters.Add(TEXT("Duration"), 10);
		Erosion.NumericParameters.Add(TEXT("Downcutting"), 0.48);
		Erosion.NumericParameters.Add(TEXT("ErosionScale"), Scale);
		Erosion.NumericParameters.Add(TEXT("Shape"), 0.55);
		Erosion.NumericParameters.Add(TEXT("ShapeSharpness"), 0.45);
		Erosion.IntegerParameters.Add(TEXT("Seed"), 1193);
		FEonformTerrainRecipe Recipe;
		Recipe.Nodes = { Base, Erosion };
		Recipe.Connections.Add(Link(Base.Id, TEXT("Out"), Erosion.Id, TEXT("Terrain")));
		Recipe.OutputNode = Erosion.Id;
		return FEonformTerrainEvaluator::Evaluate(Recipe, Context());
	};

	const FEonformTerrainEvaluationResult Fine = EvaluateScale(100.0);
	const FEonformTerrainEvaluationResult Broad = EvaluateScale(4000.0);
	TestTrue(TEXT("Fine Erosion2 evaluates"), Fine.bSuccess);
	TestTrue(TEXT("Broad Erosion2 evaluates"), Broad.bSuccess);
	const FEonformScalarField* FineHeight = Fine.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* BroadHeight = Broad.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* FineWear = Fine.Dataset.FindScalarField(EonformTerrainFieldNames::Wear);
	const FEonformScalarField* FineFlow = Fine.Dataset.FindScalarField(EonformTerrainFieldNames::Flow);
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
