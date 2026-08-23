#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaColorizeNodes.h"
#include "GaeaRadialGradientNode.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace GaeaSatMapTests
{
	const FName ProbeType(TEXT("SatMapColorProbe"));

	void RegisterProbe()
	{
		FGaeaTerrainNodeRegistry::Register(ProbeType,
			[](const FGaeaTerrainNode&, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
			{
				const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Color"));
				const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
				if (!Input || Input->Type != EGaeaTerrainValueType::Color || !Input->ColorField.IsValid())
				{
					Error = TEXT("SatMap test probe requires Color input.");
					return false;
				}

				FGaeaFieldDescriptor Descriptor;
				Descriptor.Name = GaeaTerrainFieldNames::Height;
				Descriptor.Unit = EGaeaFieldUnit::Normalized;
				Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
				FGaeaScalarField Height;
				Height.Initialize(Input->ColorField.Domain, Descriptor, 0.0f);
				for (int32 Y = 0; Y < Height.Domain.Dimensions.Y; ++Y)
				{
					for (int32 X = 0; X < Height.Domain.Dimensions.X; ++X)
					{
						const FLinearColor C = Input->ColorField.AtInterior(X, Y);
						Height.AtInterior(X, Y) = FMath::Clamp(C.GetLuminance(), 0.0f, 1.0f);
					}
				}

				FGaeaTerrainDataset Dataset;
				if (!Dataset.SetScalarField(MoveTemp(Height)))
				{
					Error = TEXT("SatMap test probe could not publish Height.");
					return false;
				}
				Out.Outputs.Add(TEXT("Terrain"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), 1.0f));
				return true;
			});
	}

	FGaeaTerrainEvaluationResult Evaluate(bool bReverse)
	{
		FGaeaTerrainNode Source;
		Source.Id = FGuid(1, 2, 3, 4);
		Source.Type = GaeaTerrainNodeTypes::RadialGradient;
		Source.NumericParameters.Add(TEXT("Scale"), 0.92);
		Source.NumericParameters.Add(TEXT("Height"), 1.0);

		FGaeaTerrainNode SatMap;
		SatMap.Id = FGuid(5, 6, 7, 8);
		SatMap.Type = GaeaTerrainNodeTypes::SatMap;
		SatMap.NameParameters.Add(TEXT("Palette"), TEXT("Alpine"));
		SatMap.IntegerParameters.Add(TEXT("Item"), 2);
		SatMap.NameParameters.Add(TEXT("Enhance"), TEXT("Autolevel"));
		SatMap.BoolParameters.Add(TEXT("Reverse"), bReverse);
		SatMap.NumericParameters.Add(TEXT("Roughness"), 0.12);

		FGaeaTerrainNode Probe;
		Probe.Id = FGuid(9, 10, 11, 12);
		Probe.Type = ProbeType;

		FGaeaTerrainRecipe Recipe;
		Recipe.Nodes = { Source, SatMap, Probe };
		Recipe.OutputNode = Probe.Id;

		FGaeaTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Out");
		A.ToNode = SatMap.Id;
		A.ToInput = TEXT("Input");
		Recipe.Connections.Add(A);

		FGaeaTerrainConnection B;
		B.FromNode = SatMap.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Probe.Id;
		B.ToInput = TEXT("Color");
		Recipe.Connections.Add(B);

		FGaeaTerrainEvaluationContext Context;
		Context.TargetResolution = FIntPoint(65, 65);
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(8000.0, 6000.0, 1800.0, 0.0);
		return FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaSatMapNodeTest,
	"CodenameGaea.Core.Graph.Colorize.SatMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSatMapNodeTest::RunTest(const FString& Parameters)
{
	RegisterGaeaRadialGradientNode();
	RegisterGaeaColorizeNodes();
	GaeaSatMapTests::RegisterProbe();

	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("SatMap descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::SatMap, Descriptor));
	TestEqual(TEXT("SatMap is a Colorize node"), Descriptor.Category, FString(TEXT("Colorize")));
	TestTrue(TEXT("SatMap exposes a Color output"), Descriptor.Outputs.ContainsByPredicate([](const FGaeaTerrainPortDescriptor& P)
	{
		return P.Name == TEXT("Out") && P.DataType == TEXT("Color");
	}));

	const FGaeaTerrainEvaluationResult First = GaeaSatMapTests::Evaluate(false);
	const FGaeaTerrainEvaluationResult Second = GaeaSatMapTests::Evaluate(false);
	const FGaeaTerrainEvaluationResult Reversed = GaeaSatMapTests::Evaluate(true);
	TestTrue(TEXT("SatMap graph evaluates"), First.bSuccess);
	TestTrue(TEXT("SatMap is deterministic"), Second.bSuccess);
	TestTrue(TEXT("SatMap Reverse evaluates"), Reversed.bSuccess);
	if (!First.bSuccess || !Second.bSuccess || !Reversed.bSuccess)
	{
		if (!First.Error.IsEmpty()) AddError(First.Error);
		if (!Second.Error.IsEmpty()) AddError(Second.Error);
		if (!Reversed.Error.IsEmpty()) AddError(Reversed.Error);
		return false;
	}

	const FGaeaScalarField* A = First.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* B = Second.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* R = Reversed.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	TestNotNull(TEXT("SatMap probe produced luminance"), A);
	TestNotNull(TEXT("Second SatMap probe produced luminance"), B);
	TestNotNull(TEXT("Reversed SatMap probe produced luminance"), R);
	if (!A || !B || !R) return false;

	TestEqual(TEXT("SatMap preserves requested width"), A->Domain.Dimensions.X, 65);
	TestEqual(TEXT("SatMap preserves requested depth"), A->Domain.Dimensions.Y, 65);

	float MinValue = TNumericLimits<float>::Max();
	float MaxValue = TNumericLimits<float>::Lowest();
	double DeterminismDifference = 0.0;
	double ReverseDifference = 0.0;
	for (int32 I = 0; I < A->Values.Num(); ++I)
	{
		MinValue = FMath::Min(MinValue, A->Values[I]);
		MaxValue = FMath::Max(MaxValue, A->Values[I]);
		DeterminismDifference += FMath::Abs(static_cast<double>(A->Values[I] - B->Values[I]));
		ReverseDifference += FMath::Abs(static_cast<double>(A->Values[I] - R->Values[I]));
	}

	TestTrue(TEXT("SatMap produces a varied natural color map"), MaxValue - MinValue > 0.08f);
	TestTrue(TEXT("SatMap output is exactly deterministic"), DeterminismDifference < 1.0e-8);
	TestTrue(TEXT("SatMap Reverse materially changes the mapping"), ReverseDifference / FMath::Max(1, A->Values.Num()) > 0.02);
	return true;
}

#endif
