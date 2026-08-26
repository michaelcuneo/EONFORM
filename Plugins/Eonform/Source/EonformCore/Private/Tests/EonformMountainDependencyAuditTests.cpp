#if WITH_DEV_AUTOMATION_TESTS

#include "EonformCracksNode.h"
#include "EonformReferenceFidelityMountainNodes.h"
#include "EonformReferenceFidelityNodes.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace GaeaMountainDependencyAuditTests
{
	FEonformTerrainNode Radial(FGuid Id)
	{
		FEonformTerrainNode N; N.Id = Id; N.Type = EonformTerrainNodeTypes::RadialGradient;
		N.NumericParameters.Add(TEXT("Scale"), 0.85); N.NumericParameters.Add(TEXT("Height"), 0.85); return N;
	}

	FEonformTerrainConnection Connect(const FGuid& From, FName Output, const FGuid& To, FName Input)
	{
		FEonformTerrainConnection C; C.FromNode = From; C.FromOutput = Output; C.ToNode = To; C.ToInput = Input; return C;
	}

	FEonformTerrainEvaluationResult Evaluate(const FEonformTerrainRecipe& Recipe, int32 Resolution = 65)
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(Resolution, Resolution);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(4000.0, 4000.0, 1800.0, 0.0);
		return FEonformTerrainEvaluator::Evaluate(Recipe, Context);
	}

	double Difference(const FEonformScalarField& A, const FEonformScalarField& B)
	{
		if (!A.IsValid() || !B.IsValid() || A.Domain != B.Domain) return 0.0;
		double Sum = 0.0;
		for (int32 Y = 0; Y < A.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < A.Domain.Dimensions.X; ++X)
			Sum += FMath::Abs(static_cast<double>(A.AtInterior(X, Y) - B.AtInterior(X, Y)));
		return Sum / FMath::Max(1, A.Domain.GetInteriorSampleCount());
	}

	FEonformTerrainEvaluationResult EvaluateModifier(FName Type, const TMap<FName,double>& Numbers, const TMap<FName,int64>& Integers = {}, const TMap<FName,FName>& Names = {})
	{
		FEonformTerrainNode Source = Radial(FGuid(1,2,3,4));
		FEonformTerrainNode Node; Node.Id = FGuid(5,6,7,8); Node.Type = Type; Node.NumericParameters = Numbers; Node.IntegerParameters = Integers; Node.NameParameters = Names;
		FEonformTerrainRecipe R; R.Nodes = {Source, Node}; R.Connections.Add(Connect(Source.Id, TEXT("Out"), Node.Id, Type == EonformTerrainNodeTypes::SlopeWarp ? TEXT("Input") : TEXT("Terrain"))); R.OutputNode = Node.Id;
		return Evaluate(R);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformMountainThermalScaleAuditTest,
	"Eonform.Core.Graph.ReferenceFidelity.ThermalScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainThermalScaleAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterEonformReferenceFidelityNodes(); RegisterEonformReferenceFidelityMountainNodes();
	const FEonformTerrainEvaluationResult Fine = EvaluateModifier(EonformTerrainNodeTypes::Thermal2, {{TEXT("FeatureScale"),20.0},{TEXT("Strength"),0.7},{TEXT("Angle"),28.0}}, {{TEXT("Duration"),16}});
	const FEonformTerrainEvaluationResult Broad = EvaluateModifier(EonformTerrainNodeTypes::Thermal2, {{TEXT("FeatureScale"),1000.0},{TEXT("Strength"),0.7},{TEXT("Angle"),28.0}}, {{TEXT("Duration"),16}});
	TestTrue(TEXT("Fine Thermal2 evaluates"), Fine.bSuccess); TestTrue(TEXT("Broad Thermal2 evaluates"), Broad.bSuccess);
	const FEonformScalarField* A = Fine.Dataset.FindScalarField(EonformTerrainFieldNames::Height); const FEonformScalarField* B = Broad.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("Physical Feature Scale materially changes Thermal2 morphology"), Difference(*A,*B) > 0.0001);

	FEonformTerrainNodeDescriptor Classic, Modern;
	TestTrue(TEXT("Thermal descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::ThermalErosion, Classic));
	TestTrue(TEXT("Thermal2 descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Thermal2, Modern));
	TestEqual(TEXT("Thermal exposes documented 11 controls"), Classic.Parameters.Num(), 11);
	TestEqual(TEXT("Thermal2 exposes documented 6 controls"), Modern.Parameters.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformMountainSlopeWarpAuditTest,
	"Eonform.Core.Graph.ReferenceFidelity.SlopeWarp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainSlopeWarpAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterEonformReferenceFidelityNodes(); RegisterEonformReferenceFidelityMountainNodes();
	FEonformTerrainNodeDescriptor D; TestTrue(TEXT("SlopeWarp descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::SlopeWarp,D));
	TestEqual(TEXT("SlopeWarp exposes documented six controls"), D.Parameters.Num(), 6);
	const FEonformTerrainEvaluationResult Off = EvaluateModifier(EonformTerrainNodeTypes::SlopeWarp, {{TEXT("Intensity"),0.0}});
	const FEonformTerrainEvaluationResult On = EvaluateModifier(EonformTerrainNodeTypes::SlopeWarp, {{TEXT("Intensity"),0.45},{TEXT("Direction"),35.0}}, {{TEXT("Iterations"),2}}, {{TEXT("Quality"),TEXT("High")},{TEXT("Antialiasing"),TEXT("X 4")}});
	const FEonformScalarField* A = Off.Dataset.FindScalarField(EonformTerrainFieldNames::Height); const FEonformScalarField* B = On.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("SlopeWarp materially changes the terrain"), Difference(*A,*B) > 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformMountainRockSurfaceAuditTest,
	"Eonform.Core.Graph.ReferenceFidelity.RockSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainRockSurfaceAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterEonformReferenceFidelityNodes(); RegisterEonformReferenceFidelityMountainNodes();
	FEonformTerrainNodeDescriptor Outcrops, Craggy;
	TestTrue(TEXT("Outcrops descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Outcrops,Outcrops));
	TestTrue(TEXT("Craggy descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Craggy,Craggy));
	TestEqual(TEXT("Outcrops exposes documented nine controls"), Outcrops.Parameters.Num(), 9);
	TestEqual(TEXT("Craggy exposes documented four controls"), Craggy.Parameters.Num(), 4);
	const FEonformTerrainEvaluationResult Sparse = EvaluateModifier(EonformTerrainNodeTypes::Outcrops, {{TEXT("Density"),0.15},{TEXT("Height"),0.6},{TEXT("Chipped"),0.0}});
	const FEonformTerrainEvaluationResult Dense = EvaluateModifier(EonformTerrainNodeTypes::Outcrops, {{TEXT("Density"),0.85},{TEXT("Height"),0.6},{TEXT("Chipped"),0.8}});
	const FEonformScalarField* A = Sparse.Dataset.FindScalarField(EonformTerrainFieldNames::Height); const FEonformScalarField* B = Dense.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("Outcrops Density/Chipped controls materially change output"), Difference(*A,*B) > 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformMountainSedimentScreeAuditTest,
	"Eonform.Core.Graph.ReferenceFidelity.SedimentScree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformMountainSedimentScreeAuditTest::RunTest(const FString& Parameters)
{
	using namespace GaeaMountainDependencyAuditTests;
	RegisterEonformReferenceFidelityNodes(); RegisterEonformReferenceFidelityMountainNodes();
	FEonformTerrainNodeDescriptor Sediments, Scree;
	TestTrue(TEXT("Sediments descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Sediments,Sediments));
	TestTrue(TEXT("Scree descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Scree,Scree));
	TestEqual(TEXT("Sediments exposes documented six controls"), Sediments.Parameters.Num(), 6);
	TestEqual(TEXT("Scree exposes documented seven controls"), Scree.Parameters.Num(), 7);

	const FEonformTerrainEvaluationResult Tight = EvaluateModifier(EonformTerrainNodeTypes::Scree, {{TEXT("Stones"),0.15},{TEXT("Spread"),0.0},{TEXT("Edge"),0.0},{TEXT("Density"),0.65}});
	const FEonformTerrainEvaluationResult Spread = EvaluateModifier(EonformTerrainNodeTypes::Scree, {{TEXT("Stones"),0.9},{TEXT("Spread"),0.9},{TEXT("Edge"),1.0},{TEXT("Density"),0.65}});
	const FEonformScalarField* A = Tight.Dataset.FindScalarField(EonformTerrainFieldNames::Height); const FEonformScalarField* B = Spread.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (A && B) TestTrue(TEXT("Scree Stones/Spread/Edge controls materially change output"), Difference(*A,*B) > 0.00001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformCracksResolutionAuditTest,
	"Eonform.Core.Graph.ReferenceFidelity.CracksResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformCracksResolutionAuditTest::RunTest(const FString& Parameters)
{
	RegisterEonformCracksNode();
	FEonformTerrainNode N; N.Id = FGuid(11,12,13,14); N.Type = EonformTerrainNodeTypes::Cracks; N.IntegerParameters.Add(TEXT("Octaves"),3);
	FEonformTerrainRecipe R; R.Nodes.Add(N); R.OutputNode=N.Id;
	FEonformTerrainEvaluationContext C; C.TargetResolution=FIntPoint(73,61); C.PhysicalMetrics=FEonformTerrainPhysicalMetrics(9000.0,5000.0,2500.0,0.0);
	const FEonformTerrainEvaluationResult Result=FEonformTerrainEvaluator::Evaluate(R,C); TestTrue(TEXT("Cracks evaluates"),Result.bSuccess);
	const FEonformScalarField* H=Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if(H){TestEqual(TEXT("Cracks honors target width"),H->Domain.Dimensions.X,73);TestEqual(TEXT("Cracks honors target height"),H->Domain.Dimensions.Y,61);}
	return true;
}

#endif
