#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformScalarField MakeWizardHeight(const FEonformGridDomain& Domain)
	{
		FEonformFieldDescriptor D;
		D.Name = EonformTerrainFieldNames::Height;
		D.Unit = EEonformFieldUnit::Normalized;
		D.Interpolation = EEonformInterpolation::Bilinear;

		FEonformScalarField Height;
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

	FEonformTerrainEvaluationContext MakeWizardContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(3200.0, 3200.0, 1400.0, 0.0);
		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(25, 25),
			FVector2d(-160000.0, -160000.0),
			FVector2d(160000.0, 160000.0));
		Context.SourceDataset.SetScalarField(MakeWizardHeight(Domain));
		return Context;
	}

	FEonformTerrainRecipe MakeWizardRecipe(bool bWizard2)
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(2001, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		FEonformTerrainNode Wizard;
		Wizard.Id = FGuid(2002, 2, 2, bWizard2 ? 22 : 2);
		Wizard.Type = bWizard2 ? EonformTerrainNodeTypes::Wizard2 : EonformTerrainNodeTypes::Wizard;
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

		FEonformTerrainConnection Connection;
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
		const FEonformTerrainEvaluationResult& Result,
		FName ProcessMaskName,
		const FEonformScalarField& SourceHeight,
		float& OutMaxMask,
		float& OutHeightDelta)
	{
		Test.TestTrue(TEXT("Wizard graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}

		const FEonformScalarField* Height = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		const FEonformScalarField* Wear = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Wear);
		const FEonformScalarField* Deposits = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Deposits);
		const FEonformScalarField* Flow = Result.Dataset.FindScalarField(EonformTerrainFieldNames::Flow);
		const FEonformScalarField* Mask = Result.Dataset.FindScalarField(ProcessMaskName);
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
	FEonformWizardCompositeTest,
	"Eonform.Core.Graph.WizardComposite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformWizardCompositeTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor WizardDescriptor;
	FEonformTerrainNodeDescriptor Wizard2Descriptor;
	TestTrue(TEXT("Wizard descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Wizard, WizardDescriptor));
	TestTrue(TEXT("Wizard2 descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Wizard2, Wizard2Descriptor));
	TestEqual(TEXT("Wizard parameter contract"), WizardDescriptor.Parameters.Num(), 13);
	TestEqual(TEXT("Wizard2 parameter contract"), Wizard2Descriptor.Parameters.Num(), 8);

	const FEonformTerrainEvaluationContext Context = MakeWizardContext();
	const FEonformScalarField* SourceHeight = Context.SourceDataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (!TestNotNull(TEXT("Synthetic source Height exists"), SourceHeight) || !SourceHeight) return false;

	const FEonformTerrainEvaluationResult WizardResult = FEonformTerrainEvaluator::Evaluate(MakeWizardRecipe(false), Context);
	float WizardMask = 0.0f;
	float WizardDelta = 0.0f;
	if (!ValidateWizardResult(*this, WizardResult, TEXT("WizardProcessMask"), *SourceHeight, WizardMask, WizardDelta)) return false;

	const FEonformTerrainEvaluationResult Wizard2Result = FEonformTerrainEvaluator::Evaluate(MakeWizardRecipe(true), Context);
	float Wizard2Mask = 0.0f;
	float Wizard2Delta = 0.0f;
	if (!ValidateWizardResult(*this, Wizard2Result, TEXT("Wizard2ProcessMask"), *SourceHeight, Wizard2Mask, Wizard2Delta)) return false;

	TestTrue(TEXT("Wizard and Wizard2 resolve distinct erosion recipes"),
		FMath::Abs(WizardMask - Wizard2Mask) > 0.0001f || FMath::Abs(WizardDelta - Wizard2Delta) > 0.0001f);
	return true;
}

#endif
