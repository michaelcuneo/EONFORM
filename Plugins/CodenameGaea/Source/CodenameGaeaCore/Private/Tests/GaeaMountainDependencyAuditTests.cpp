#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaCracksNode.h"
#include "GaeaReferenceFidelityMountainNodes.h"
#include "GaeaReferenceFidelityNodes.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace GaeaMountainDependencyAuditTests
{
	FGaeaTerrainNode Radial(FGuid Id)
	{
		FGaeaTerrainNode N; N.Id = Id; N.Type = GaeaTerrainNodeTypes::RadialGradient;
		N.NumericParameters.Add(TEXT("Scale"), 0.85); N.NumericParameters.Add(TEXT("Height"), 0.85); return N;
	}

	FGaeaTerrainConnection Connect(const FGuid& From, FName Output, const FGuid& To, FName Input)
	{
		FGaeaTerrainConnection C; C.FromNode = From; C.FromOutput = Output; C.ToNode = To; C.ToInput = Input; return C;
	}

	FGaeaTerrainEvaluationResult Evaluate(const FGaeaTerrainRecipe& Recipe, int32 Resolution = 65)
	{
		FGaeaTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(Resolution, Resolution);
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(4000.0, 4000.0, 1800.0, 0.0);
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	}

	double Difference(const FGaeaScalarField& A, const FGaeaScalarField& B)
	{
		if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain) return 0.0;
		double Sum = 0.0;
		for (int32 Y = 0; Y < A.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < A.Domain.Dimensions.X; ++X)
			Sum += FMath::Abs(static_cast<double>(A.AtInterior(X, Y) - B.AtInterior(X, Y)));
		return Sum / FMath::Max(1, A.Domain.GetInteriorSampleCount());
	}

	FGaeaTerrainEvaluationResult EvaluateModifier(FName Type, const TMap<FName,double>& Numbers, const TMap<FName,int64>& Integers = {}, const TMap<FName,FName>& Names = {})
	{
		FGaeaTerrainNode Source = Radial(FGuid(1,2,3,4));
		FGaeaTerrainNode Node; Node.Id = FGuid(5,6,7,8); Node.Type = Type; Node.NumericParameters = Numbers; Node.IntegerParameters = Integers; Node.NameParameters = Names;
		FGaeaTerrainRecipe R; R.Nodes = {Source, Node}; R.Connections.Add(Connect(Source.Id, TEXT("Out"), Node.Id, Type == GaeaTerrainNodeTypes::SlopeWarp ? TEXT("Input") : TEXT("Terrain"))); R.OutputNode = Node.Id;
		return Evaluate(R);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaMountainThermalScaleAuditTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.ThermalScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaMountainThermalScaleAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterGaeaReferenceFidelityNodes(); RegisterGaeaReferenceFidelityMountainNodes();
	const FGaeaTerrainEvaluationResult Fine = EvaluateModifier(GaeaTerrainNodeTypes::Thermal2, {{TEXT("FeatureScale"),20.0},{TEXT("Strength"),0.7},{TEXT("Angle"),28.0}}, {{TEXT("Duration"),16}});
	const FGaeaTerrainEvaluationResult Broad = EvaluateModifier(GaeaTerrainNodeTypes::Thermal2, {{TEXT("FeatureScale"),1000.0},{TEXT("Strength"),0.7},{TEXT("Angle"),28.0}}, {{TEXT("Duration"),16}});
	TestTrue(TEXT("Fine Thermal2 evaluates"), Fine.bSuccess); TestTrue(TEXT("Broad Thermal2 evaluates"), Broad.bSuccess);
	const FGaeaScalarField* A = Fine.Dataset.FindScalarField(GaeaTerrainFieldNames::Height); const FGaeaScalarField* B = Broad.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("Physical Feature Scale materially changes Thermal2 morphology"), Difference(*A,*B) > 0.0001);

	FGaeaTerrainNodeDescriptor Classic, Modern;
	TestTrue(TEXT("Thermal descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ThermalErosion, Classic));
	TestTrue(TEXT("Thermal2 descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Thermal2, Modern));
	TestEqual(TEXT("Thermal exposes documented 11 controls"), Classic.Parameters.Num(), 11);
	TestEqual(TEXT("Thermal2 exposes documented 6 controls"), Modern.Parameters.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaMountainSlopeWarpAuditTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.SlopeWarp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaMountainSlopeWarpAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterGaeaReferenceFidelityNodes(); RegisterGaeaReferenceFidelityMountainNodes();
	FGaeaTerrainNodeDescriptor D; TestTrue(TEXT("SlopeWarp descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::SlopeWarp,D));
	TestEqual(TEXT("SlopeWarp exposes documented six controls"), D.Parameters.Num(), 6);
	const FGaeaTerrainEvaluationResult Off = EvaluateModifier(GaeaTerrainNodeTypes::SlopeWarp, {{TEXT("Intensity"),0.0}});
	const FGaeaTerrainEvaluationResult On = EvaluateModifier(GaeaTerrainNodeTypes::SlopeWarp, {{TEXT("Intensity"),0.45},{TEXT("Direction"),35.0}}, {{TEXT("Iterations"),2}}, {{TEXT("Quality"),TEXT("High")},{TEXT("Antialiasing"),TEXT("X 4")}});
	const FGaeaScalarField* A = Off.Dataset.FindScalarField(GaeaTerrainFieldNames::Height); const FGaeaScalarField* B = On.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("SlopeWarp materially changes the terrain"), Difference(*A,*B) > 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaMountainRockSurfaceAuditTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.RockSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaMountainRockSurfaceAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterGaeaReferenceFidelityNodes(); RegisterGaeaReferenceFidelityMountainNodes();
	FGaeaTerrainNodeDescriptor Outcrops, Craggy;
	TestTrue(TEXT("Outcrops descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Outcrops,Outcrops));
	TestTrue(TEXT("Craggy descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Craggy,Craggy));
	TestEqual(TEXT("Outcrops exposes documented nine controls"), Outcrops.Parameters.Num(), 9);
	TestEqual(TEXT("Craggy exposes documented four controls"), Craggy.Parameters.Num(), 4);
	const FGaeaTerrainEvaluationResult Sparse = EvaluateModifier(GaeaTerrainNodeTypes::Outcrops, {{TEXT("Density"),0.15},{TEXT("Height"),0.6},{TEXT("Chipped"),0.0}});
	const FGaeaTerrainEvaluationResult Dense = EvaluateModifier(GaeaTerrainNodeTypes::Outcrops, {{TEXT("Density"),0.85},{TEXT("Height"),0.6},{TEXT("Chipped"),0.8}});
	const FGaeaScalarField* A = Sparse.Dataset.FindScalarField(GaeaTerrainFieldNames::Height); const FGaeaScalarField* B = Dense.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("Outcrops Density/Chipped controls materially change output"), Difference(*A,*B) > 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaMountainSedimentScreeAuditTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.SedimentScree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaMountainSedimentScreeAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterGaeaReferenceFidelityNodes(); RegisterGaeaReferenceFidelityMountainNodes();
	FGaeaTerrainNodeDescriptor Sediments, Scree;
	TestTrue(TEXT("Sediments descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Sediments,Sediments));
	TestTrue(TEXT("Scree descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Scree,Scree));
	TestEqual(TEXT("Sediments exposes documented six controls"), Sediments.Parameters.Num(), 6);
	TestEqual(TEXT("Scree exposes documented seven controls"), Scree.Parameters.Num(), 7);

	const FGaeaTerrainEvaluationResult Tight = EvaluateModifier(GaeaTerrainNodeTypes::Scree, {{TEXT("Stones"),0.15},{TEXT("Spread"),0.0},{TEXT("Edge"),0.0},{TEXT("Density"),0.65}});
	const FGaeaTerrainEvaluationResult Spread = EvaluateModifier(GaeaTerrainNodeTypes::Scree, {{TEXT("Stones"),0.9},{TEXT("Spread"),0.9},{TEXT("Edge"),1.0},{TEXT("Density"),0.65}});
	const FGaeaScalarField* A = Tight.Dataset.FindScalarField(GaeaTerrainFieldNames::Height); const FGaeaScalarField* B = Spread.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("Scree Stones/Spread/Edge controls materially change output"), Difference(*A,*B) > 0.00001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaCracksResolutionAuditTest,
	"CodenameGaea.Core.Graph.ReferenceFidelity.CracksResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaCracksResolutionAuditTest::RunTest(const FString& Parameters)
{
	RegisterGaeaCracksNode();
	FGaeaTerrainNode N; N.Id = FGuid(11,12,13,14); N.Type = GaeaTerrainNodeTypes::Cracks; N.IntegerParameters.Add(TEXT("Octaves"),3);
	FGaeaTerrainRecipe R; R.Nodes.Add(N); R.OutputNode=N.Id;
	FGaeaTerrainEvaluationContext C; C.TargetResolution=FIntPoint(73,61); C.PhysicalMetrics=FGaeaTerrainPhysicalMetrics(9000.0,5000.0,2500.0,0.0);
	const FGaeaTerrainEvaluationResult Result=FGaeaTerrainEvaluator::Evaluate(R,C); TestTrue(TEXT("Cracks evaluates"),Result.bSuccess);
	const FGaeaScalarField* H=Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if(H){TestEqual(TEXT("Cracks honors target width"),H->Domain.Dimensions.X,73);TestEqual(TEXT("Cracks honors target height"),H->Domain.Dimensions.Y,61);}
	return true;
}

#endif
