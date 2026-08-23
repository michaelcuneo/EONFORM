#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaReferenceFidelityNodes.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaUtilityNodes.h"
#include "Misc/AutomationTest.h"

namespace GaeaReferenceFidelityTests
{
	FGaeaTerrainEvaluationResult EvaluateSingle(const FGaeaTerrainNode& Node, int32 Resolution = 65)
	{
		FGaeaTerrainRecipe Recipe;
		Recipe.Nodes.Add(Node);
		Recipe.OutputNode = Node.Id;
		FGaeaTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(Resolution, Resolution);
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(10000.0, 6000.0, 3000.0, 0.0);
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
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

	FGaeaTerrainNode MakeVoronoi(int32 Seed)
	{
		FGaeaTerrainNode N;
		N.Id = FGuid(0x564F524F, 0x4E4F4900, static_cast<uint32>(Seed), 1u);
		N.Type = GaeaTerrainNodeTypes::Voronoi;
		N.IntegerParameters.Add(TEXT("Seed"), Seed);
		N.NameParameters.Add(TEXT("Form"), TEXT("P"));
		N.NumericParameters.Add(TEXT("Scale"), 0.65);
		return N;
	}

	FGaeaTerrainNode MakeConstant(FGuid Id, double Height)
	{
		FGaeaTerrainNode N;
		N.Id = Id;
		N.Type = GaeaTerrainNodeTypes::Constant;
		N.NameParameters.Add(TEXT("Output"), TEXT("Height"));
		N.NumericParameters.Add(TEXT("Height"), Height);
		N.IntegerParameters.Add(TEXT("Resolution"), 33);
		N.NumericParameters.Add(TEXT("WorldSize"), 100000.0);
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaReferencePrimitiveFidelityTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.Primitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaReferencePrimitiveFidelityTest::RunTest(const FString& Parameters)
{
	using namespace GaeaReferenceFidelityTests;
	RegisterGaeaReferenceFidelityNodes();

	FGaeaTerrainNodeDescriptor RadialDescriptor;
	TestTrue(TEXT("RadialGradient descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::RadialGradient, RadialDescriptor));
	TestEqual(TEXT("RadialGradient has documented four controls"), RadialDescriptor.Parameters.Num(), 4);

	FGaeaTerrainNode Radial;
	Radial.Id = FGuid(1, 2, 3, 4);
	Radial.Type = GaeaTerrainNodeTypes::RadialGradient;
	Radial.NumericParameters.Add(TEXT("Scale"), 0.65);
	Radial.NumericParameters.Add(TEXT("Height"), 0.8);
	const FGaeaTerrainEvaluationResult RadialResult = EvaluateSingle(Radial, 73);
	TestTrue(TEXT("RadialGradient evaluates"), RadialResult.bSuccess);
	const FGaeaScalarField* RadialHeight = RadialResult.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("RadialGradient publishes Height"), RadialHeight);
	if (RadialHeight)
	{
		TestEqual(TEXT("RadialGradient honors graph target width"), RadialHeight->Domain.Dimensions.X, 73);
		TestEqual(TEXT("RadialGradient honors graph target depth"), RadialHeight->Domain.Dimensions.Y, 73);
		TestTrue(TEXT("RadialGradient uses physical rectangular domain"), FMath::IsNearlyEqual(RadialHeight->Domain.WorldSize().X, 1000000.0, 1.0));
		TestTrue(TEXT("RadialGradient uses physical rectangular depth"), FMath::IsNearlyEqual(RadialHeight->Domain.WorldSize().Y, 600000.0, 1.0));
	}

	FGaeaTerrainNodeDescriptor VoronoiDescriptor;
	TestTrue(TEXT("Voronoi descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Voronoi, VoronoiDescriptor));
	TestEqual(TEXT("Voronoi exposes documented fifteen controls"), VoronoiDescriptor.Parameters.Num(), 15);

	FGaeaTerrainNode BaseNode = MakeVoronoi(991);
	FGaeaTerrainNode WarpedNode = BaseNode;
	WarpedNode.Id = FGuid(5, 6, 7, 8);
	WarpedNode.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
	WarpedNode.NumericParameters.Add(TEXT("WarpFrequency"), 1.7);
	WarpedNode.NumericParameters.Add(TEXT("WarpAmplitude"), 0.9);
	WarpedNode.IntegerParameters.Add(TEXT("WarpOctaves"), 4);
	FGaeaTerrainNode AnisotropicNode = BaseNode;
	AnisotropicNode.Id = FGuid(9, 10, 11, 12);
	AnisotropicNode.NumericParameters.Add(TEXT("ScaleX"), 2.7);
	AnisotropicNode.NumericParameters.Add(TEXT("ScaleY"), 0.55);
	FGaeaTerrainNode ShiftedNode = BaseNode;
	ShiftedNode.Id = FGuid(13, 14, 15, 16);
	ShiftedNode.NumericParameters.Add(TEXT("X"), 0.18);
	ShiftedNode.NumericParameters.Add(TEXT("Y"), -0.11);

	const FGaeaTerrainEvaluationResult Base = EvaluateSingle(BaseNode);
	const FGaeaTerrainEvaluationResult Warped = EvaluateSingle(WarpedNode);
	const FGaeaTerrainEvaluationResult Anisotropic = EvaluateSingle(AnisotropicNode);
	const FGaeaTerrainEvaluationResult Shifted = EvaluateSingle(ShiftedNode);
	TestTrue(TEXT("Voronoi base evaluates"), Base.bSuccess);
	TestTrue(TEXT("Voronoi warped evaluates"), Warped.bSuccess);
	TestTrue(TEXT("Voronoi anisotropic evaluates"), Anisotropic.bSuccess);
	TestTrue(TEXT("Voronoi shifted evaluates"), Shifted.bSuccess);
	const FGaeaScalarField* BH = Base.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* WH = Warped.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* AH = Anisotropic.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* SH = Shifted.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (BH && WH && AH && SH)
	{
		TestTrue(TEXT("Voronoi internal warp materially changes output"), MeanAbsoluteDifference(*BH, *WH) > 0.015);
		TestTrue(TEXT("Voronoi non-uniform scale materially changes output"), MeanAbsoluteDifference(*BH, *AH) > 0.015);
		TestTrue(TEXT("Voronoi X/Y transform materially changes output"), MeanAbsoluteDifference(*BH, *SH) > 0.015);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaReferenceCombineMaskFidelityTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.CombineMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaReferenceCombineMaskFidelityTest::RunTest(const FString& Parameters)
{
	using namespace GaeaReferenceFidelityTests;
	RegisterGaeaReferenceFidelityNodes();
	RegisterGaeaUtilityNodes();

	FGaeaTerrainNode A = MakeConstant(FGuid(0xA1, 1, 1, 1), 0.8);
	FGaeaTerrainNode B = MakeConstant(FGuid(0xB1, 1, 1, 1), -0.8);
	FGaeaTerrainNode MaskTerrain = MakeConstant(FGuid(0xC1, 1, 1, 1), 1.0);
	FGaeaTerrainNode Extract;
	Extract.Id = FGuid(0xD1, 1, 1, 1);
	Extract.Type = GaeaTerrainNodeTypes::DataExtractor;
	Extract.NameParameters.Add(TEXT("Field"), GaeaTerrainFieldNames::Height);
	FGaeaTerrainNode Combine;
	Combine.Id = FGuid(0xE1, 1, 1, 1);
	Combine.Type = GaeaTerrainNodeTypes::Combine;
	Combine.NameParameters.Add(TEXT("Mode"), TEXT("Blend"));
	Combine.NameParameters.Add(TEXT("Output"), TEXT("None"));
	Combine.NumericParameters.Add(TEXT("Ratio"), 0.5);

	FGaeaTerrainRecipe Recipe;
	Recipe.Nodes = { A, B, MaskTerrain, Extract, Combine };
	auto Connect = [&Recipe](const FGuid& From, FName FromOutput, const FGuid& To, FName ToInput)
	{
		FGaeaTerrainConnection C;
		C.FromNode = From;
		C.FromOutput = FromOutput;
		C.ToNode = To;
		C.ToInput = ToInput;
		Recipe.Connections.Add(C);
	};
	Connect(A.Id, TEXT("Out"), Combine.Id, TEXT("Input1"));
	Connect(B.Id, TEXT("Out"), Combine.Id, TEXT("Input2"));
	Connect(MaskTerrain.Id, TEXT("Out"), Extract.Id, TEXT("Terrain"));
	Connect(Extract.Id, TEXT("Out"), Combine.Id, TEXT("Mask"));
	Recipe.OutputNode = Combine.Id;

	FGaeaTerrainEvaluationContext Context;
	const FGaeaTerrainEvaluationResult WhiteMask = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combine white-mask graph evaluates"), WhiteMask.bSuccess);
	const FGaeaScalarField* White = WhiteMask.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (White)
	{
		TestTrue(TEXT("White Combine mask selects Input1"), White->AtInterior(16, 16) > 0.70f);
	}

	for (FGaeaTerrainNode& N : Recipe.Nodes)
	{
		if (N.Id == MaskTerrain.Id) N.NumericParameters[TEXT("Height")] = 0.0;
	}
	const FGaeaTerrainEvaluationResult BlackMask = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combine black-mask graph evaluates"), BlackMask.bSuccess);
	const FGaeaScalarField* Black = BlackMask.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (Black)
	{
		TestTrue(TEXT("Black Combine mask selects Input2"), Black->AtInterior(16, 16) < -0.70f);
	}
	return true;
}

#endif
