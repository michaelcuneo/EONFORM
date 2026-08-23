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
	FGaeaScalarField MakeWizardHeight(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor D;
		D.Name = GaeaTerrainFieldNames::Height;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField Height;
		Height.Initialize(Domain, D, 0.0f);
		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const float NX = static_cast<float>(X) / static_cast<float>(Domain.Dimensions.X - 1);
				const float NY = static_cast<float>(Y) / static_cast<float>(Domain.Dimensions.Y - 1);
				const float Valley = 0.18f * FMath::Abs(NX - 0.5f);
				const float Ridge = 0.07f * FMath::Sin(NX * UE_TWO_PI * 2.0f) * FMath::Sin(NY * UE_PI);
				const float RegionalSlope = 0.42f * (1.0f - NY);
				Height.AtInterior(X, Y) = FMath::Clamp(0.12f + RegionalSlope + Valley + Ridge, 0.02f, 0.92f);
			}
		}
		return Height;
	}

	FGaeaTerrainEvaluationContext MakeWizardContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(3200.0, 3200.0, 1400.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(25, 25),
			FVector2d(-160000.0, -160000.0),
			FVector2d(160000.0, 160000.0));
		Context.SourceDataset.SetScalarField(MakeWizardHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeWizardRecipe(bool bWizard2)
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(2001, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode Wizard;
		Wizard.Id = FGuid(2002, 2, 2, bWizard2 ? 22 : 2);
		Wizard.Type = bWizard2 ? GaeaTerrainNodeTypes::Wizard2 : GaeaTerrainNodeTypes::Wizard;
		Wizard.IntegerParameters.Add(TEXT("Seed"), 7331);
		if (bWizard2)
		{
			Wizard.NameParameters.Add(TEXT("Power"), TEXT("Ultra"));
			Wizard.NameParameters.Add(TEXT("Depth"), TEXT("High"));
			Wizard.NameParameters.Add(TEXT("Scale"), TEXT("High"));
			Wizard.NameParameters.Add(TEXT("Deposits"), TEXT("High"));
			Wizard.NameParameters.Add(TEXT("Flow"), TEXT("Ultra"));
			Wizard.NameParameters.Add(TEXT("Shape"), TEXT("High"));
			Wizard.NameParameters.Add(TEXT("Detail"), TEXT("High"));
		}
		else
		{
			Wizard.NameParameters.Add(TEXT("Preset"), TEXT("RiverCarving"));
			Wizard.NumericParameters.Add(TEXT("Strength"), 0.85);
			Wizard.NumericParameters.Add(TEXT("Depth"), 0.85);
			Wizard.NumericParameters.Add(TEXT("Width"), 0.55);
			Wizard.NumericParameters.Add(TEXT("Rivers"), 0.9);
			Wizard.NumericParameters.Add(TEXT("Furrows"), 0.5);
			Wizard.NumericParameters.Add(TEXT("Talus"), 0.35);
			Wizard.IntegerParameters.Add(TEXT("Duration"), 36);
		}

		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Wizard.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Wizard };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = Wizard.Id;
		return Recipe;
	}

	bool ValidateWizardResult(
		FAutomationTestBase& Test,
		const FGaeaTerrainEvaluationResult& Result,
		FName ProcessMaskName,
		const FGaeaScalarField& SourceHeight,
		float& OutMaxMask,
		float& OutHeightDelta)
	{
		Test.TestTrue(TEXT("Wizard graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}

		const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Wear = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Wear);
		const FGaeaScalarField* Deposits = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Deposits);
		const FGaeaScalarField* Flow = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Flow);
		const FGaeaScalarField* Mask = Result.Dataset.FindScalarField(ProcessMaskName);
		Test.TestNotNull(TEXT("Wizard publishes Height"), Height);
		Test.TestNotNull(TEXT("Wizard publishes Wear"), Wear);
		Test.TestNotNull(TEXT("Wizard publishes Deposits"), Deposits);
		Test.TestNotNull(TEXT("Wizard publishes Flow"), Flow);
		Test.TestNotNull(TEXT("Wizard publishes ProcessMask"), Mask);
		if (!Height || !Wear || !Deposits || !Flow || !Mask) return false;

		OutMaxMask = 0.0f;
		OutHeightDelta = 0.0f;
		for (int32 I = 0; I < Height->Values.Num(); ++I)
		{
			Test.TestTrue(TEXT("Wizard Height remains finite"), FMath::IsFinite(Height->Values[I]));
			Test.TestTrue(TEXT("Wizard process mask remains finite"), FMath::IsFinite(Mask->Values[I]));
			OutMaxMask = FMath::Max(OutMaxMask, Mask->Values[I]);
			OutHeightDelta += FMath::Abs(Height->Values[I] - SourceHeight.Values[I]);
		}
		Test.TestTrue(TEXT("Wizard selects meaningful regional process areas"), OutMaxMask > 0.05f);
		Test.TestTrue(TEXT("Wizard modifies terrain"), OutHeightDelta > UE_SMALL_NUMBER);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaWizardCompositeTest,
	"CodenameGaea.Core.Graph.WizardComposite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaWizardCompositeTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor WizardDescriptor;
	FGaeaTerrainNodeDescriptor Wizard2Descriptor;
	TestTrue(TEXT("Wizard descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Wizard, WizardDescriptor));
	TestTrue(TEXT("Wizard2 descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Wizard2, Wizard2Descriptor));
	TestEqual(TEXT("Wizard parameter contract"), WizardDescriptor.Parameters.Num(), 13);
	TestEqual(TEXT("Wizard2 parameter contract"), Wizard2Descriptor.Parameters.Num(), 8);

	const FGaeaTerrainEvaluationContext Context = MakeWizardContext();
	const FGaeaScalarField* SourceHeight = Context.SourceDataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (!TestNotNull(TEXT("Synthetic source Height exists"), SourceHeight) || !SourceHeight) return false;

	const FGaeaTerrainEvaluationResult WizardResult = FGaeaTerrainEvaluator::Evaluate(MakeWizardRecipe(false), Context);
	float WizardMask = 0.0f;
	float WizardDelta = 0.0f;
	if (!ValidateWizardResult(*this, WizardResult, TEXT("WizardProcessMask"), *SourceHeight, WizardMask, WizardDelta)) return false;

	const FGaeaTerrainEvaluationResult Wizard2Result = FGaeaTerrainEvaluator::Evaluate(MakeWizardRecipe(true), Context);
	float Wizard2Mask = 0.0f;
	float Wizard2Delta = 0.0f;
	if (!ValidateWizardResult(*this, Wizard2Result, TEXT("Wizard2ProcessMask"), *SourceHeight, Wizard2Mask, Wizard2Delta)) return false;

	TestTrue(TEXT("Wizard and Wizard2 resolve distinct erosion recipes"),
		FMath::Abs(WizardMask - Wizard2Mask) > 0.0001f || FMath::Abs(WizardDelta - Wizard2Delta) > 0.0001f);
	return true;
}

#endif
