#if WITH_DEV_AUTOMATION_TESTS

#include "EonformCombineNode.h"
#include "EonformConstantNode.h"
#include "EonformTerrainDomainScaling.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "Misc/AutomationTest.h"

namespace
{
	const FName ResolutionProbeType(TEXT("EonformTestResolutionProbe"));

	FEonformTerrainRecipe MakeMismatchedCombineRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode A;
		A.Id = FGuid(9101, 1, 1, 1);
		A.Type = EonformTerrainNodeTypes::Constant;
		A.NameParameters.Add(TEXT("Output"), TEXT("Height"));
		A.IntegerParameters.Add(TEXT("Resolution"), 17);
		A.NumericParameters.Add(TEXT("WorldSize"), 16000.0);
		A.NumericParameters.Add(TEXT("Height"), 0.25);

		FEonformTerrainNode B;
		B.Id = FGuid(9102, 2, 2, 2);
		B.Type = EonformTerrainNodeTypes::Constant;
		B.NameParameters.Add(TEXT("Output"), TEXT("Height"));
		B.IntegerParameters.Add(TEXT("Resolution"), 41);
		B.NumericParameters.Add(TEXT("WorldSize"), 82000.0);
		B.NumericParameters.Add(TEXT("Height"), 0.75);

		FEonformTerrainNode Combine;
		Combine.Id = FGuid(9103, 3, 3, 3);
		Combine.Type = EonformTerrainNodeTypes::Combine;
		Combine.NameParameters.Add(TEXT("Mode"), TEXT("Max"));
		Combine.NumericParameters.Add(TEXT("Ratio"), 1.0);

		FEonformTerrainConnection CA;
		CA.FromNode = A.Id;
		CA.FromOutput = TEXT("Out");
		CA.ToNode = Combine.Id;
		CA.ToInput = TEXT("Input1");

		FEonformTerrainConnection CB;
		CB.FromNode = B.Id;
		CB.FromOutput = TEXT("Out");
		CB.ToNode = Combine.Id;
		CB.ToInput = TEXT("Input2");

		Recipe.Nodes = { A, B, Combine };
		Recipe.Connections = { CA, CB };
		Recipe.OutputNode = Combine.Id;
		return Recipe;
	}

	FEonformTerrainEvaluationContext OutputContext(int32 Resolution)
	{
		FEonformTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(Resolution, Resolution);
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(12000.0, 8000.0, 2500.0, 0.0);
		return Context;
	}

	void RegisterResolutionProbe()
	{
		FEonformTerrainNodeRegistry::Register(
			ResolutionProbeType,
			[](const FEonformTerrainNode& Node,
				const FEonformTerrainNodeInputs&,
				const FEonformTerrainEvaluationContext&,
				FEonformTerrainNodeEvaluation& Out,
				FString& Error)
			{
				const int32 SeenResolution = static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 0));
				const FEonformGridDomain Domain = FEonformGridDomain::Make(
					FIntPoint(2, 2), FVector2d(-1.0, -1.0), FVector2d(1.0, 1.0));
				FEonformFieldDescriptor Descriptor;
				Descriptor.Name = EonformTerrainFieldNames::Height;
				Descriptor.Unit = EEonformFieldUnit::Normalized;
				Descriptor.Interpolation = EEonformInterpolation::Bilinear;
				FEonformScalarField Height;
				Height.Initialize(Domain, Descriptor, static_cast<float>(SeenResolution) / 100.0f);
				FEonformTerrainDataset Dataset;
				if (!Dataset.SetScalarField(MoveTemp(Height)))
				{
					Error = TEXT("Resolution probe could not publish Height.");
					return false;
				}
				Out.Outputs.Add(TEXT("Terrain"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), 1000.0f));
				return true;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformSelectedOutputDomainCombineTest,
	"Eonform.Core.Graph.SelectedOutputDomain.CombineScalesMismatchedInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformSelectedOutputDomainCombineTest::RunTest(const FString& Parameters)
{
	RegisterEonformConstantNode();
	RegisterEonformCombineNode();

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(
		MakeMismatchedCombineRecipe(),
		OutputContext(65));
	TestTrue(TEXT("Mismatched source domains combine successfully"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Combined Height exists"), Height);
	if (!Height) return false;
	TestEqual(TEXT("Selected output resolution wins"), Height->Domain.Dimensions, FIntPoint(65, 65));
	TestEqual(TEXT("Selected output world width wins"), Height->Domain.WorldSize().X, 1200000.0);
	TestEqual(TEXT("Selected output world depth wins"), Height->Domain.WorldSize().Y, 800000.0);
	TestTrue(TEXT("Max combine preserved higher constant"), FMath::IsNearlyEqual(Height->AtInterior(32, 32), 0.75f, 1.e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformNativeDomainCombineCompatibilityTest,
	"Eonform.Core.Graph.SelectedOutputDomain.CombineNativeModeStillScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformNativeDomainCombineCompatibilityTest::RunTest(const FString& Parameters)
{
	RegisterEonformConstantNode();
	RegisterEonformCombineNode();
	FEonformTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics();

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(
		MakeMismatchedCombineRecipe(),
		Context);
	TestTrue(TEXT("Native mismatched domains are scaled instead of rejected"), Result.bSuccess);
	if (!Result.bSuccess) AddError(Result.Error);
	return Result.bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformSelectedOutputResolutionOverridesLegacyNodeTest,
	"Eonform.Core.Graph.SelectedOutputDomain.LegacyResolutionIsOverriddenBeforeEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformSelectedOutputResolutionOverridesLegacyNodeTest::RunTest(const FString& Parameters)
{
	RegisterResolutionProbe();
	FEonformTerrainRecipe Recipe;
	FEonformTerrainNode Probe;
	Probe.Id = FGuid(9201, 4, 4, 4);
	Probe.Type = ResolutionProbeType;
	Probe.IntegerParameters.Add(TEXT("Resolution"), 11);
	Recipe.Nodes.Add(Probe);
	Recipe.OutputNode = Probe.Id;

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, OutputContext(73));
	TestTrue(TEXT("Resolution probe evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}
	const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Probe Height exists"), Height);
	if (!Height) return false;
	TestEqual(TEXT("Probe is published at selected output resolution"), Height->Domain.Dimensions, FIntPoint(73, 73));
	TestTrue(TEXT("Node saw selected resolution before it generated"), FMath::IsNearlyEqual(Height->AtInterior(36, 36), 0.73f, 1.e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformIncrementalOutputResolutionInvalidationTest,
	"Eonform.Core.Graph.SelectedOutputDomain.ResolutionInvalidatesReachableCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformIncrementalOutputResolutionInvalidationTest::RunTest(const FString& Parameters)
{
	RegisterEonformConstantNode();
	RegisterEonformCombineNode();
	FEonformTerrainEvaluationCache Cache;
	FEonformTerrainEvaluationContext Context = OutputContext(33);
	Context.CacheContextRevision = 17;

	const FEonformTerrainEvaluationResult First = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeMismatchedCombineRecipe(), Context, Cache);
	TestTrue(TEXT("Initial incremental solve succeeds"), First.bSuccess);
	if (!First.bSuccess)
	{
		AddError(First.Error);
		return false;
	}
	TestEqual(TEXT("All three reachable nodes evaluate initially"), First.EvaluatedNodeCount, 3);

	const FEonformTerrainEvaluationResult Cached = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeMismatchedCombineRecipe(), Context, Cache);
	TestTrue(TEXT("Repeated incremental solve succeeds"), Cached.bSuccess);
	TestEqual(TEXT("Repeated solve reuses all reachable nodes"), Cached.CachedNodeCount, 3);

	Context.TargetResolution = FIntPoint(71, 71);
	const FEonformTerrainEvaluationResult Rescaled = FEonformTerrainEvaluator::EvaluateIncremental(
		MakeMismatchedCombineRecipe(), Context, Cache);
	TestTrue(TEXT("Resolution change re-evaluates successfully"), Rescaled.bSuccess);
	if (!Rescaled.bSuccess)
	{
		AddError(Rescaled.Error);
		return false;
	}
	TestEqual(TEXT("Resolution change recalculates all reachable nodes"), Rescaled.EvaluatedNodeCount, 3);
	TestEqual(TEXT("Resolution change has no stale cache hits"), Rescaled.CachedNodeCount, 0);

	const FEonformScalarField* Height = Rescaled.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Rescaled Height exists"), Height);
	if (Height) TestEqual(TEXT("Rescaled output matches selected resolution"), Height->Domain.Dimensions, FIntPoint(71, 71));
	return Height != nullptr;
}

#endif
