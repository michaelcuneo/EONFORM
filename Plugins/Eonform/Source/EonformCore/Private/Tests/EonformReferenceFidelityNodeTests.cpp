#if WITH_DEV_AUTOMATION_TESTS

#include "EonformReferenceFidelityNodes.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformUtilityNodes.h"
#include "Misc/AutomationTest.h"

namespace EonformReferenceFidelityTests
{
	FEonformTerrainEvaluationResult EvaluateSingle(const FEonformTerrainNode& Node, int32 Resolution = 65)
	{
		FEonformTerrainRecipe Recipe;
		Recipe.Nodes.Add(Node);
		Recipe.OutputNode = Node.Id;
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(Resolution, Resolution);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(10000.0, 6000.0, 3000.0, 0.0);
		return FEonformTerrainEvaluator::Evaluate(Recipe, Context);
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

	FEonformTerrainNode MakeVoronoi(int32 Seed)
	{
		FEonformTerrainNode N;
		N.Id = FGuid(0x564F524F, 0x4E4F4900, static_cast<uint32>(Seed), 1u);
		N.Type = EonformTerrainNodeTypes::Voronoi;
		N.IntegerParameters.Add(TEXT("Seed"), Seed);
		N.NameParameters.Add(TEXT("Form"), TEXT("P"));
		N.NumericParameters.Add(TEXT("Scale"), 0.65);
		return N;
	}

	FEonformTerrainNode MakeConstant(FGuid Id, double Height)
	{
		FEonformTerrainNode N;
		N.Id = Id;
		N.Type = EonformTerrainNodeTypes::Constant;
		N.NameParameters.Add(TEXT("Output"), TEXT("Height"));
		N.NumericParameters.Add(TEXT("Height"), Height);
		N.IntegerParameters.Add(TEXT("Resolution"), 33);
		N.NumericParameters.Add(TEXT("WorldSize"), 100000.0);
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformReferencePrimitiveFidelityTest,
	"Eonform.Core.Graph.ReferenceFidelity.Primitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformReferencePrimitiveFidelityTest::RunTest(const FString& Parameters)
{
	using namespace EonformReferenceFidelityTests;
	RegisterEonformReferenceFidelityNodes();

	FEonformTerrainNodeDescriptor RadialDescriptor;
	TestTrue(TEXT("RadialGradient descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::RadialGradient, RadialDescriptor));
	TestEqual(TEXT("RadialGradient has documented four controls"), RadialDescriptor.Parameters.Num(), 4);

	FEonformTerrainNode Radial;
	Radial.Id = FGuid(1, 2, 3, 4);
	Radial.Type = EonformTerrainNodeTypes::RadialGradient;
	Radial.NumericParameters.Add(TEXT("Scale"), 0.65);
	Radial.NumericParameters.Add(TEXT("Height"), 0.8);
	const FEonformTerrainEvaluationResult RadialResult = EvaluateSingle(Radial, 73);
	TestTrue(TEXT("RadialGradient evaluates"), RadialResult.bSuccess);
	const FEonformScalarField* RadialHeight = RadialResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("RadialGradient publishes Height"), RadialHeight);
	if (RadialHeight)
	{
		TestEqual(TEXT("RadialGradient honors graph target width"), RadialHeight->Domain.Dimensions.X, 73);
		TestEqual(TEXT("RadialGradient honors graph target depth"), RadialHeight->Domain.Dimensions.Y, 73);
		TestTrue(TEXT("RadialGradient uses physical rectangular domain"), FMath::IsNearlyEqual(RadialHeight->Domain.WorldSize().X, 1000000.0, 1.0));
		TestTrue(TEXT("RadialGradient uses physical rectangular depth"), FMath::IsNearlyEqual(RadialHeight->Domain.WorldSize().Y, 600000.0, 1.0));
	}

	FEonformTerrainNodeDescriptor VoronoiDescriptor;
	TestTrue(TEXT("Voronoi descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Voronoi, VoronoiDescriptor));
	TestEqual(TEXT("Voronoi exposes documented fifteen controls"), VoronoiDescriptor.Parameters.Num(), 15);

	FEonformTerrainNode BaseNode = MakeVoronoi(991);
	FEonformTerrainNode WarpedNode = BaseNode;
	WarpedNode.Id = FGuid(5, 6, 7, 8);
	WarpedNode.NameParameters.Add(TEXT("WarpType"), TEXT("Complex"));
	WarpedNode.NumericParameters.Add(TEXT("WarpFrequency"), 1.7);
	WarpedNode.NumericParameters.Add(TEXT("WarpAmplitude"), 0.9);
	WarpedNode.IntegerParameters.Add(TEXT("WarpOctaves"), 4);
	FEonformTerrainNode AnisotropicNode = BaseNode;
	AnisotropicNode.Id = FGuid(9, 10, 11, 12);
	AnisotropicNode.NumericParameters.Add(TEXT("ScaleX"), 2.7);
	AnisotropicNode.NumericParameters.Add(TEXT("ScaleY"), 0.55);
	FEonformTerrainNode ShiftedNode = BaseNode;
	ShiftedNode.Id = FGuid(13, 14, 15, 16);
	ShiftedNode.NumericParameters.Add(TEXT("X"), 0.18);
	ShiftedNode.NumericParameters.Add(TEXT("Y"), -0.11);

	const FEonformTerrainEvaluationResult Base = EvaluateSingle(BaseNode);
	const FEonformTerrainEvaluationResult Warped = EvaluateSingle(WarpedNode);
	const FEonformTerrainEvaluationResult Anisotropic = EvaluateSingle(AnisotropicNode);
	const FEonformTerrainEvaluationResult Shifted = EvaluateSingle(ShiftedNode);
	TestTrue(TEXT("Voronoi base evaluates"), Base.bSuccess);
	TestTrue(TEXT("Voronoi warped evaluates"), Warped.bSuccess);
	TestTrue(TEXT("Voronoi anisotropic evaluates"), Anisotropic.bSuccess);
	TestTrue(TEXT("Voronoi shifted evaluates"), Shifted.bSuccess);
	const FEonformScalarField* BH = Base.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* WH = Warped.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* AH = Anisotropic.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	const FEonformScalarField* SH = Shifted.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (BH && WH && AH && SH)
	{
		TestTrue(TEXT("Voronoi internal warp materially changes output"), MeanAbsoluteDifference(*BH, *WH) > 0.015);
		TestTrue(TEXT("Voronoi non-uniform scale materially changes output"), MeanAbsoluteDifference(*BH, *AH) > 0.015);
		TestTrue(TEXT("Voronoi X/Y transform materially changes output"), MeanAbsoluteDifference(*BH, *SH) > 0.015);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformReferenceCombineMaskFidelityTest,
	"Eonform.Core.Graph.ReferenceFidelity.CombineMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformReferenceCombineMaskFidelityTest::RunTest(const FString& Parameters)
{
	using namespace EonformReferenceFidelityTests;
	RegisterEonformUtilityNodes();
	RegisterEonformReferenceFidelityNodes();

	FEonformTerrainNode A = MakeConstant(FGuid(0xA1, 1, 1, 1), 0.8);
	FEonformTerrainNode B = MakeConstant(FGuid(0xB1, 1, 1, 1), -0.8);
	FEonformTerrainNode MaskTerrain = MakeConstant(FGuid(0xC1, 1, 1, 1), 1.0);
	FEonformTerrainNode Extract;
	Extract.Id = FGuid(0xD1, 1, 1, 1);
	Extract.Type = EonformTerrainNodeTypes::DataExtractor;
	Extract.NameParameters.Add(TEXT("Field"), EonformTerrainFieldNames::Height);
	FEonformTerrainNode Combine;
	Combine.Id = FGuid(0xE1, 1, 1, 1);
	Combine.Type = EonformTerrainNodeTypes::Combine;
	Combine.NameParameters.Add(TEXT("Mode"), TEXT("Blend"));
	Combine.NameParameters.Add(TEXT("Output"), TEXT("None"));
	Combine.NumericParameters.Add(TEXT("Ratio"), 0.5);

	FEonformTerrainRecipe Recipe;
	Recipe.Nodes = { A, B, MaskTerrain, Extract, Combine };
	auto Connect = [&Recipe](const FGuid& From, FName FromOutput, const FGuid& To, FName ToInput)
	{
		FEonformTerrainConnection C;
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

	FEonformTerrainEvaluationContext Context;
	const FEonformTerrainEvaluationResult WhiteMask = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combine white-mask graph evaluates"), WhiteMask.bSuccess);
	const FEonformScalarField* White = WhiteMask.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (White)
	{
		TestTrue(TEXT("White Combine mask selects Input1"), White->AtInterior(16, 16) > 0.70f);
	}

	for (FEonformTerrainNode& N : Recipe.Nodes)
	{
		if (N.Id == MaskTerrain.Id) N.NumericParameters[TEXT("Height")] = 0.0;
	}
	const FEonformTerrainEvaluationResult BlackMask = FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Combine black-mask graph evaluates"), BlackMask.bSuccess);
	const FEonformScalarField* Black = BlackMask.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (Black)
	{
		TestTrue(TEXT("Black Combine mask selects Input2"), Black->AtInterior(16, 16) < -0.70f);
	}
	return true;
}

#endif
