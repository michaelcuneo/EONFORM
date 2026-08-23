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
	FGaeaScalarField MakeCryosphereField(const FGaeaGridDomain& Domain, FName Name, float InitialValue = 0.0f)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	FGaeaTerrainEvaluationContext MakeCryosphereContext()
	{
		FGaeaTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(1000.0, 1000.0, 1800.0, 0.0);

		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(11, 11),
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
		FGaeaScalarField Height = MakeCryosphereField(Domain, GaeaTerrainFieldNames::Height, 0.0f);

		// Broad cold upland with a shallow central trough. This gives Snowfield
		// room to settle material and gives Glacier a valley-biased flow route.
		for (int32 Y = 0; Y < 11; ++Y)
		{
			for (int32 X = 0; X < 11; ++X)
			{
				const float NX = static_cast<float>(X - 5) / 5.0f;
				const float NY = static_cast<float>(Y - 5) / 5.0f;
				const float Ridge = 0.78f - 0.18f * FMath::Abs(NY);
				const float Trough = 0.08f * FMath::Abs(NX);
				Height.AtInterior(X, Y) = FMath::Clamp(Ridge + Trough, 0.35f, 0.9f);
			}
		}

		Context.SourceDataset.SetScalarField(MoveTemp(Height));
		return Context;
	}

	FGaeaTerrainRecipe MakeCryosphereRecipe()
	{
		FGaeaTerrainRecipe Recipe;

		FGaeaTerrainNode Source;
		Source.Id = FGuid(801, 1, 1, 1);
		Source.Type = GaeaTerrainNodeTypes::SourceDataset;

		FGaeaTerrainNode Snow;
		Snow.Id = FGuid(802, 2, 2, 2);
		Snow.Type = GaeaTerrainNodeTypes::Snow;
		Snow.NumericParameters.Add(TEXT("BaseTemperatureC"), 2.0);
		Snow.NumericParameters.Add(TEXT("LapseRateCPerKm"), 8.0);
		Snow.NumericParameters.Add(TEXT("Precipitation"), 1.0);
		Snow.NumericParameters.Add(TEXT("MaxDepthMeters"), 4.0);
		Snow.BoolParameters.Add(TEXT("AffectHeight"), true);

		FGaeaTerrainNode Snowfield;
		Snowfield.Id = FGuid(803, 3, 3, 3);
		Snowfield.Type = GaeaTerrainNodeTypes::Snowfield;
		Snowfield.NumericParameters.Add(TEXT("Iterations"), 8.0);
		Snowfield.NumericParameters.Add(TEXT("TransportStrength"), 0.25);
		Snowfield.NumericParameters.Add(TEXT("Compaction"), 0.05);
		Snowfield.NumericParameters.Add(TEXT("MeltRateMetersPerC"), 0.0);
		Snowfield.BoolParameters.Add(TEXT("AffectHeight"), true);

		FGaeaTerrainNode Glacier;
		Glacier.Id = FGuid(804, 4, 4, 4);
		Glacier.Type = GaeaTerrainNodeTypes::Glacier;
		Glacier.NumericParameters.Add(TEXT("Iterations"), 8.0);
		Glacier.NumericParameters.Add(TEXT("FirnDepthMeters"), 0.2);
		Glacier.NumericParameters.Add(TEXT("IceCompaction"), 0.8);
		Glacier.NumericParameters.Add(TEXT("MeltRateMetersPerC"), 0.0);
		Glacier.NumericParameters.Add(TEXT("ErosionStrength"), 0.05);
		Glacier.BoolParameters.Add(TEXT("AffectHeight"), true);

		FGaeaTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Terrain");
		A.ToNode = Snow.Id;
		A.ToInput = TEXT("Terrain");

		FGaeaTerrainConnection B;
		B.FromNode = Snow.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Snowfield.Id;
		B.ToInput = TEXT("Terrain");

		FGaeaTerrainConnection C;
		C.FromNode = Snowfield.Id;
		C.FromOutput = TEXT("Out");
		C.ToNode = Glacier.Id;
		C.ToInput = TEXT("Terrain");

		Recipe.Nodes = { Source, Snow, Snowfield, Glacier };
		Recipe.Connections = { A, B, C };
		Recipe.OutputNode = Glacier.Id;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaCryosphereChainTest,
	"CodenameGaea.Core.Graph.CryosphereChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaCryosphereChainTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainNodeDescriptor SnowDescriptor;
	FGaeaTerrainNodeDescriptor SnowfieldDescriptor;
	FGaeaTerrainNodeDescriptor GlacierDescriptor;
	TestTrue(TEXT("Snow descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Snow, SnowDescriptor));
	TestTrue(TEXT("Snowfield descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Snowfield, SnowfieldDescriptor));
	TestTrue(TEXT("Glacier descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::Glacier, GlacierDescriptor));
	TestEqual(TEXT("Snow parameter contract"), SnowDescriptor.Parameters.Num(), 10);
	TestEqual(TEXT("Snowfield parameter contract"), SnowfieldDescriptor.Parameters.Num(), 8);
	TestEqual(TEXT("Glacier parameter contract"), GlacierDescriptor.Parameters.Num(), 13);

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(MakeCryosphereRecipe(), MakeCryosphereContext());
	TestTrue(TEXT("Cryosphere graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Snowfield = Result.Dataset.FindScalarField(TEXT("Snowfield"));
	const FGaeaScalarField* SnowfieldDepth = Result.Dataset.FindScalarField(TEXT("SnowfieldDepth"));
	const FGaeaScalarField* Glacier = Result.Dataset.FindScalarField(TEXT("Glacier"));
	const FGaeaScalarField* IceDepth = Result.Dataset.FindScalarField(TEXT("IceDepth"));
	const FGaeaScalarField* GlacialErosion = Result.Dataset.FindScalarField(TEXT("GlacialErosion"));
	const FGaeaScalarField* IceFlow = Result.Dataset.FindScalarField(TEXT("IceFlow"));
	TestNotNull(TEXT("Snowfield is published through the chain"), Snowfield);
	TestNotNull(TEXT("SnowfieldDepth is published through the chain"), SnowfieldDepth);
	TestNotNull(TEXT("Glacier is published"), Glacier);
	TestNotNull(TEXT("IceDepth is published"), IceDepth);
	TestNotNull(TEXT("GlacialErosion is published"), GlacialErosion);
	TestNotNull(TEXT("IceFlow is published"), IceFlow);
	if (!Glacier || !IceDepth || !GlacialErosion || !IceFlow) return false;

	float MaxIce = 0.0f;
	float MaxGlacier = 0.0f;
	for (const float Value : IceDepth->Values) MaxIce = FMath::Max(MaxIce, Value);
	for (const float Value : Glacier->Values) MaxGlacier = FMath::Max(MaxGlacier, Value);
	TestTrue(TEXT("Persistent deep snow forms physical ice"), MaxIce > 0.1f);
	TestTrue(TEXT("Glacier mask contains active glacier terrain"), MaxGlacier > 0.5f);
	return true;
}

#endif
