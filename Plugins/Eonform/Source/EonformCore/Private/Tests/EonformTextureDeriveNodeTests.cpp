#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformTextureDeriveNodes.h"
#include "Misc/AutomationTest.h"

namespace
{
	const FEonformTerrainParameterDescriptor* FindParameter(const FEonformTerrainNodeDescriptor& Descriptor, FName Name)
	{
		return Descriptor.Parameters.FindByPredicate([Name](const FEonformTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == Name;
		});
	}

	FEonformScalarField MakeTextureHeight(const FEonformGridDomain& Domain)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Height;
		Height.Initialize(Domain, Descriptor, 0.0f);

		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const float NX = static_cast<float>(X) / static_cast<float>(Domain.Dimensions.X - 1);
				const float NY = static_cast<float>(Y) / static_cast<float>(Domain.Dimensions.Y - 1);
				const float Ridge = 0.52f * FMath::Exp(-FMath::Square((NX - 0.48f) / 0.16f));
				const float Summit = 0.34f * FMath::Exp(-(
					FMath::Square((NX - 0.33f) / 0.12f)
					+ FMath::Square((NY - 0.30f) / 0.14f)));
				const float Valley = -0.20f * FMath::Exp(-FMath::Square((NX - 0.68f) / 0.09f));
				const float Regional = 0.18f * (1.0f - NY);
				Height.AtInterior(X, Y) = FMath::Clamp(-0.12f + Ridge + Summit + Valley + Regional, -0.85f, 0.95f);
			}
		}
		return Height;
	}

	FEonformTerrainEvaluationContext MakeTextureContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(12000.0, 12000.0, 2200.0, 0.0);
		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(41, 41),
			FVector2d(-600000.0, -600000.0),
			FVector2d(600000.0, 600000.0));
		Context.SourceDataset.SetScalarField(MakeTextureHeight(Domain));
		return Context;
	}

	FEonformTerrainRecipe MakeSingleTerrainDeriveRecipe(FName Type)
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source;
		Source.Id = FGuid(5101, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		FEonformTerrainNode Derive;
		Derive.Id = FGuid(5102, 2, 2, GetTypeHash(Type));
		Derive.Type = Type;
		if (Type == EonformTerrainNodeTypes::TextureBase)
		{
			Derive.NumericParameters.Add(TEXT("Slope"), 0.65);
			Derive.NumericParameters.Add(TEXT("Flows"), 0.55);
			Derive.NumericParameters.Add(TEXT("Soil"), 0.50);
			Derive.NumericParameters.Add(TEXT("Patches"), 0.40);
			Derive.NumericParameters.Add(TEXT("Chaos"), 0.20);
			Derive.NumericParameters.Add(TEXT("Peaks"), 0.45);
			Derive.NameParameters.Add(TEXT("Accentuate"), TEXT("New"));
			Derive.NameParameters.Add(TEXT("Enhance"), TEXT("Autolevel"));
			Derive.BoolParameters.Add(TEXT("Reverse"), false);
			Derive.IntegerParameters.Add(TEXT("Seed"), 9127);
		}
		else if (Type == EonformTerrainNodeTypes::Texturizer)
		{
			Derive.NameParameters.Add(TEXT("Style"), TEXT("G"));
			Derive.NumericParameters.Add(TEXT("Factor"), 0.72);
			Derive.NumericParameters.Add(TEXT("Secondary"), 0.52);
			Derive.IntegerParameters.Add(TEXT("Seed"), 4411);
			Derive.NumericParameters.Add(TEXT("Flows"), 0.65);
			Derive.NumericParameters.Add(TEXT("Slope"), 0.55);
			Derive.NumericParameters.Add(TEXT("Soil"), 0.48);
		}

		FEonformTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Derive.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Derive };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = Derive.Id;
		return Recipe;
	}

	FEonformTerrainRecipe MakeColorThresholdRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source;
		Source.Id = FGuid(5201, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		FEonformTerrainNode Normals;
		Normals.Id = FGuid(5202, 2, 2, 2);
		Normals.Type = EonformTerrainNodeTypes::Normals;
		Normals.NameParameters.Add(TEXT("Type"), TEXT("Standard"));
		Normals.NameParameters.Add(TEXT("Handedness"), TEXT("Left"));
		Normals.NameParameters.Add(TEXT("UpAxis"), TEXT("ZUp"));

		FEonformTerrainNode Threshold;
		Threshold.Id = FGuid(5203, 3, 3, 3);
		Threshold.Type = EonformTerrainNodeTypes::ColorThreshold;
		Threshold.ColorParameters.Add(TEXT("Start"), FLinearColor(0.5f, 0.5f, 1.0f, 1.0f));
		Threshold.NumericParameters.Add(TEXT("Falloff"), 0.48);

		FEonformTerrainConnection SourceToNormals;
		SourceToNormals.FromNode = Source.Id;
		SourceToNormals.FromOutput = TEXT("Terrain");
		SourceToNormals.ToNode = Normals.Id;
		SourceToNormals.ToInput = TEXT("Terrain");

		FEonformTerrainConnection NormalColorToThreshold;
		NormalColorToThreshold.FromNode = Normals.Id;
		NormalColorToThreshold.FromOutput = TEXT("Normal");
		NormalColorToThreshold.ToNode = Threshold.Id;
		NormalColorToThreshold.ToInput = TEXT("Color");

		FEonformTerrainConnection TerrainToThreshold;
		TerrainToThreshold.FromNode = Normals.Id;
		TerrainToThreshold.FromOutput = TEXT("Out");
		TerrainToThreshold.ToNode = Threshold.Id;
		TerrainToThreshold.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Normals, Threshold };
		Recipe.Connections = { SourceToNormals, NormalColorToThreshold, TerrainToThreshold };
		Recipe.OutputNode = Threshold.Id;
		return Recipe;
	}

	bool ValidateNormalizedSemantic(FAutomationTestBase& Test, const FEonformTerrainEvaluationResult& Result, FName FieldName)
	{
		Test.TestTrue(TEXT("Derive graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}

		const FEonformScalarField* Field = Result.Dataset.FindScalarField(FieldName);
		Test.TestNotNull(*FString::Printf(TEXT("%s field exists"), *FieldName.ToString()), Field);
		if (!Field) return false;

		float MinValue = 1.0f;
		float MaxValue = 0.0f;
		double Total = 0.0;
		for (const float V : Field->Values)
		{
			Test.TestTrue(TEXT("Derived value is finite"), FMath::IsFinite(V));
			Test.TestTrue(TEXT("Derived value is normalized"), V >= 0.0f && V <= 1.0f);
			MinValue = FMath::Min(MinValue, V);
			MaxValue = FMath::Max(MaxValue, V);
			Total += V;
		}
		Test.TestTrue(TEXT("Derived field contains a useful distribution"), MaxValue - MinValue > 0.05f);
		Test.TestTrue(TEXT("Derived field is non-empty"), Total > 0.05);
		return true;
	}

	void VerifyHeightInvalidation(FAutomationTestBase& Test, const FEonformTerrainEvaluationResult& Result, FName FieldName)
	{
		FEonformTerrainDataset Dataset = Result.Dataset;
		const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height) return;
		FEonformScalarField Replacement = *Height;
		Replacement.AtInterior(Replacement.Domain.Dimensions.X / 2, Replacement.Domain.Dimensions.Y / 2) += 0.001f;
		Test.TestTrue(TEXT("Replacement Height publishes"), Dataset.SetScalarField(MoveTemp(Replacement)));
		Test.TestFalse(*FString::Printf(TEXT("%s invalidates after Height changes"), *FieldName.ToString()), Dataset.HasScalarField(FieldName));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTextureDeriveContractsTest,
	"Eonform.Core.Graph.TextureDeriveContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTextureDeriveContractsTest::RunTest(const FString& Parameters)
{
	// Tests remain self-contained even if this subset is run before post-engine registration.
	RegisterEonformTextureDeriveNodes();

	FEonformTerrainNodeDescriptor Peaks;
	FEonformTerrainNodeDescriptor Rock;
	FEonformTerrainNodeDescriptor Soil;
	FEonformTerrainNodeDescriptor TextureBase;
	FEonformTerrainNodeDescriptor Texturizer;
	FEonformTerrainNodeDescriptor ColorThreshold;

	TestTrue(TEXT("Peaks descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Peaks, Peaks));
	TestTrue(TEXT("RockMap descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::RockMap, Rock));
	TestTrue(TEXT("Soil descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Soil, Soil));
	TestTrue(TEXT("TextureBase descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::TextureBase, TextureBase));
	TestTrue(TEXT("Texturizer descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Texturizer, Texturizer));
	TestTrue(TEXT("ColorThreshold descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::ColorThreshold, ColorThreshold));

	TestNotNull(TEXT("Peaks exposes Falloff"), FindParameter(Peaks, TEXT("Falloff")));
	const FEonformTerrainParameterDescriptor* Precise = FindParameter(Peaks, TEXT("Precise"));
	TestNotNull(TEXT("Peaks exposes Precise"), Precise);
	if (Precise) TestEqual(TEXT("Precise is boolean"), Precise->Type, EEonformTerrainParameterType::Boolean);
	TestNull(TEXT("Peaks no longer exposes Threshold"), FindParameter(Peaks, TEXT("Threshold")));

	TestNotNull(TEXT("RockMap exposes Coverage"), FindParameter(Rock, TEXT("Coverage")));
	TestNotNull(TEXT("RockMap exposes Density"), FindParameter(Rock, TEXT("Density")));
	TestNull(TEXT("RockMap no longer exposes Exposure"), FindParameter(Rock, TEXT("Exposure")));
	TestNull(TEXT("RockMap no longer exposes Steepness"), FindParameter(Rock, TEXT("Steepness")));

	TestNotNull(TEXT("Soil exposes Amount"), FindParameter(Soil, TEXT("Amount")));
	TestNotNull(TEXT("Soil exposes Bias"), FindParameter(Soil, TEXT("Bias")));
	TestNull(TEXT("Soil no longer exposes Coverage"), FindParameter(Soil, TEXT("Coverage")));
	TestNull(TEXT("Soil no longer exposes ValleyBias"), FindParameter(Soil, TEXT("ValleyBias")));

	TestEqual(TEXT("TextureBase category"), TextureBase.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("TextureBase current parameter count"), TextureBase.Parameters.Num(), 10);
	for (const FName Name : { FName(TEXT("Slope")), FName(TEXT("Flows")), FName(TEXT("Soil")), FName(TEXT("Patches")), FName(TEXT("Chaos")), FName(TEXT("Peaks")), FName(TEXT("Accentuate")), FName(TEXT("Enhance")), FName(TEXT("Reverse")), FName(TEXT("Seed")) })
	{
		TestNotNull(*FString::Printf(TEXT("TextureBase exposes %s"), *Name.ToString()), FindParameter(TextureBase, Name));
	}

	TestEqual(TEXT("Texturizer category"), Texturizer.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("Texturizer current parameter count"), Texturizer.Parameters.Num(), 7);
	const FEonformTerrainParameterDescriptor* Style = FindParameter(Texturizer, TEXT("Style"));
	TestNotNull(TEXT("Texturizer exposes Style"), Style);
	if (Style) TestEqual(TEXT("Texturizer exposes twelve styles"), Style->NameOptions.Num(), 12);

	TestEqual(TEXT("ColorThreshold category"), ColorThreshold.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("ColorThreshold current parameter count"), ColorThreshold.Parameters.Num(), 2);
	const FEonformTerrainParameterDescriptor* Start = FindParameter(ColorThreshold, TEXT("Start"));
	TestNotNull(TEXT("ColorThreshold exposes Start"), Start);
	if (Start) TestEqual(TEXT("Start is a first-class color parameter"), Start->Type, EEonformTerrainParameterType::Color);
	TestNotNull(TEXT("ColorThreshold exposes Falloff"), FindParameter(ColorThreshold, TEXT("Falloff")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTextureDeriveBehaviorTest,
	"Eonform.Core.Graph.TextureDeriveBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTextureDeriveBehaviorTest::RunTest(const FString& Parameters)
{
	RegisterEonformTextureDeriveNodes();
	const FEonformTerrainEvaluationContext Context = MakeTextureContext();

	const FEonformTerrainEvaluationResult BaseResult = FEonformTerrainEvaluator::Evaluate(
		MakeSingleTerrainDeriveRecipe(EonformTerrainNodeTypes::TextureBase),
		Context);
	if (!ValidateNormalizedSemantic(*this, BaseResult, EonformTerrainFieldNames::TextureBase)) return false;

	const FEonformTerrainEvaluationResult TexturizerResult = FEonformTerrainEvaluator::Evaluate(
		MakeSingleTerrainDeriveRecipe(EonformTerrainNodeTypes::Texturizer),
		Context);
	if (!ValidateNormalizedSemantic(*this, TexturizerResult, EonformTerrainFieldNames::Texturizer)) return false;

	const FEonformTerrainEvaluationResult ColorResult = FEonformTerrainEvaluator::Evaluate(MakeColorThresholdRecipe(), Context);
	if (!ValidateNormalizedSemantic(*this, ColorResult, EonformTerrainFieldNames::ColorThreshold)) return false;

	VerifyHeightInvalidation(*this, BaseResult, EonformTerrainFieldNames::TextureBase);
	VerifyHeightInvalidation(*this, TexturizerResult, EonformTerrainFieldNames::Texturizer);
	VerifyHeightInvalidation(*this, ColorResult, EonformTerrainFieldNames::ColorThreshold);
	return true;
}

#endif
