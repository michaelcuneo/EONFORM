#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaGridDomain.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTextureDeriveNodes.h"
#include "Misc/AutomationTest.h"

namespace
{
	const FGaeaTerrainParameterDescriptor* FindParameter(const FGaeaTerrainNodeDescriptor& Descriptor, FName Name)
	{
		return Descriptor.Parameters.FindByPredicate([Name](const FGaeaTerrainParameterDescriptor& Parameter)
		{
			return Parameter.Name == Name;
		});
	}

	FGaeaScalarField MakeTextureHeight(const FGaeaGridDomain& Domain)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
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

	FGaeaTerrainEvaluationContext MakeTextureContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(12000.0, 12000.0, 2200.0, 0.0);
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(41, 41),
			FVector2d(-600000.0, -600000.0),
			FVector2d(600000.0, 600000.0));
		Context.SourceDataset.SetScalarField(MakeTextureHeight(Domain));
		return Context;
	}

	FGaeaTerrainRecipe MakeSingleTerrainDeriveRecipe(FName Type)
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(5101, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode Derive;
		Derive.Id = FGuid(5102, 2, 2, GetTypeHash(Type));
		Derive.Type = Type;
		if (Type == GaeaTerrainNodeTypes::TextureBase)
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
		else if (Type == GaeaTerrainNodeTypes::Texturizer)
		{
			Derive.NameParameters.Add(TEXT("Style"), TEXT("G"));
			Derive.NumericParameters.Add(TEXT("Factor"), 0.72);
			Derive.NumericParameters.Add(TEXT("Secondary"), 0.52);
			Derive.IntegerParameters.Add(TEXT("Seed"), 4411);
			Derive.NumericParameters.Add(TEXT("Flows"), 0.65);
			Derive.NumericParameters.Add(TEXT("Slope"), 0.55);
			Derive.NumericParameters.Add(TEXT("Soil"), 0.48);
		}

		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = Derive.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Derive };
		Recipe.Connections = { Connection };
		Recipe.OutputNode = Derive.Id;
		return Recipe;
	}

	FGaeaTerrainRecipe MakeColorThresholdRecipe()
	{
		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(5201, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode Normals;
		Normals.Id = FGuid(5202, 2, 2, 2);
		Normals.Type = GaeaTerrainNodeTypes::Normals;
		Normals.NameParameters.Add(TEXT("Type"), TEXT("Standard"));
		Normals.NameParameters.Add(TEXT("Handedness"), TEXT("Left"));
		Normals.NameParameters.Add(TEXT("UpAxis"), TEXT("ZUp"));

		FGaeaTerrainNode Threshold;
		Threshold.Id = FGuid(5203, 3, 3, 3);
		Threshold.Type = GaeaTerrainNodeTypes::ColorThreshold;
		Threshold.ColorParameters.Add(TEXT("Start"), FLinearColor(0.5f, 0.5f, 1.0f, 1.0f));
		Threshold.NumericParameters.Add(TEXT("Falloff"), 0.48);

		FGaeaTerrainConnection SourceToNormals;
		SourceToNormals.FromNode = Source.Id;
		SourceToNormals.FromOutput = TEXT("Terrain");
		SourceToNormals.ToNode = Normals.Id;
		SourceToNormals.ToInput = TEXT("Terrain");

		FGaeaTerrainConnection NormalColorToThreshold;
		NormalColorToThreshold.FromNode = Normals.Id;
		NormalColorToThreshold.FromOutput = TEXT("Normal");
		NormalColorToThreshold.ToNode = Threshold.Id;
		NormalColorToThreshold.ToInput = TEXT("Color");

		FGaeaTerrainConnection TerrainToThreshold;
		TerrainToThreshold.FromNode = Normals.Id;
		TerrainToThreshold.FromOutput = TEXT("Out");
		TerrainToThreshold.ToNode = Threshold.Id;
		TerrainToThreshold.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Normals, Threshold };
		Recipe.Connections = { SourceToNormals, NormalColorToThreshold, TerrainToThreshold };
		Recipe.OutputNode = Threshold.Id;
		return Recipe;
	}

	bool ValidateNormalizedSemantic(FAutomationTestBase& Test, const FGaeaTerrainEvaluationResult& Result, FName FieldName)
	{
		Test.TestTrue(TEXT("Derive graph evaluates"), Result.bSuccess);
		if (!Result.bSuccess)
		{
			Test.AddError(Result.Error);
			return false;
		}

		const FGaeaScalarField* Field = Result.Dataset.FindScalarField(FieldName);
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

	void VerifyHeightInvalidation(FAutomationTestBase& Test, const FGaeaTerrainEvaluationResult& Result, FName FieldName)
	{
		FGaeaTerrainDataset Dataset = Result.Dataset;
		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height) return;
		FGaeaScalarField Replacement = *Height;
		Replacement.AtInterior(Replacement.Domain.Dimensions.X / 2, Replacement.Domain.Dimensions.Y / 2) += 0.001f;
		Test.TestTrue(TEXT("Replacement Height publishes"), Dataset.SetScalarField(MoveTemp(Replacement)));
		Test.TestFalse(*FString::Printf(TEXT("%s invalidates after Height changes"), *FieldName.ToString()), Dataset.HasScalarField(FieldName));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTextureDeriveContractsTest,
	"CodenameGaea.Core.Graph.TextureDeriveContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTextureDeriveContractsTest::RunTest(const FString& Parameters)
{
	// Tests remain self-contained even if this subset is run before post-engine registration.
	RegisterGaeaTextureDeriveNodes();

	FGaeaTerrainNodeDescriptor Peaks;
	FGaeaTerrainNodeDescriptor Rock;
	FGaeaTerrainNodeDescriptor Soil;
	FGaeaTerrainNodeDescriptor TextureBase;
	FGaeaTerrainNodeDescriptor Texturizer;
	FGaeaTerrainNodeDescriptor ColorThreshold;

	TestTrue(TEXT("Peaks descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Peaks, Peaks));
	TestTrue(TEXT("RockMap descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::RockMap, Rock));
	TestTrue(TEXT("Soil descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Soil, Soil));
	TestTrue(TEXT("TextureBase descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::TextureBase, TextureBase));
	TestTrue(TEXT("Texturizer descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Texturizer, Texturizer));
	TestTrue(TEXT("ColorThreshold descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ColorThreshold, ColorThreshold));

	TestNotNull(TEXT("Peaks exposes Falloff"), FindParameter(Peaks, TEXT("Falloff")));
	const FGaeaTerrainParameterDescriptor* Precise = FindParameter(Peaks, TEXT("Precise"));
	TestNotNull(TEXT("Peaks exposes Precise"), Precise);
	if (Precise) TestEqual(TEXT("Precise is boolean"), Precise->Type, EGaeaTerrainParameterType::Boolean);
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
	const FGaeaTerrainParameterDescriptor* Style = FindParameter(Texturizer, TEXT("Style"));
	TestNotNull(TEXT("Texturizer exposes Style"), Style);
	if (Style) TestEqual(TEXT("Texturizer exposes twelve styles"), Style->NameOptions.Num(), 12);

	TestEqual(TEXT("ColorThreshold category"), ColorThreshold.Category, FString(TEXT("Derive")));
	TestEqual(TEXT("ColorThreshold current parameter count"), ColorThreshold.Parameters.Num(), 2);
	const FGaeaTerrainParameterDescriptor* Start = FindParameter(ColorThreshold, TEXT("Start"));
	TestNotNull(TEXT("ColorThreshold exposes Start"), Start);
	if (Start) TestEqual(TEXT("Start is a first-class color parameter"), Start->Type, EGaeaTerrainParameterType::Color);
	TestNotNull(TEXT("ColorThreshold exposes Falloff"), FindParameter(ColorThreshold, TEXT("Falloff")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTextureDeriveBehaviorTest,
	"CodenameGaea.Core.Graph.TextureDeriveBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTextureDeriveBehaviorTest::RunTest(const FString& Parameters)
{
	RegisterGaeaTextureDeriveNodes();
	const FGaeaTerrainEvaluationContext Context = MakeTextureContext();

	const FGaeaTerrainEvaluationResult BaseResult = FGaeaTerrainEvaluator::Evaluate(
		MakeSingleTerrainDeriveRecipe(GaeaTerrainNodeTypes::TextureBase),
		Context);
	if (!ValidateNormalizedSemantic(*this, BaseResult, GaeaTerrainFieldNames::TextureBase)) return false;

	const FGaeaTerrainEvaluationResult TexturizerResult = FGaeaTerrainEvaluator::Evaluate(
		MakeSingleTerrainDeriveRecipe(GaeaTerrainNodeTypes::Texturizer),
		Context);
	if (!ValidateNormalizedSemantic(*this, TexturizerResult, GaeaTerrainFieldNames::Texturizer)) return false;

	const FGaeaTerrainEvaluationResult ColorResult = FGaeaTerrainEvaluator::Evaluate(MakeColorThresholdRecipe(), Context);
	if (!ValidateNormalizedSemantic(*this, ColorResult, GaeaTerrainFieldNames::ColorThreshold)) return false;

	VerifyHeightInvalidation(*this, BaseResult, GaeaTerrainFieldNames::TextureBase);
	VerifyHeightInvalidation(*this, TexturizerResult, GaeaTerrainFieldNames::Texturizer);
	VerifyHeightInvalidation(*this, ColorResult, GaeaTerrainFieldNames::ColorThreshold);
	return true;
}

#endif
