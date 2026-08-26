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
	FEonformScalarField MakeCryosphereField(const FEonformGridDomain& Domain, FName Name, float InitialValue = 0.0f)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, InitialValue);
		return Field;
	}

	FEonformTerrainEvaluationContext MakeCryosphereContext()
	{
		FEonformTerrainEvaluationContext Context;
		Context.HeightScale = 10000.0f;
		Context.PhysicalMetrics = FEonformTerrainPhysicalMetrics(1000.0, 1000.0, 1800.0, 0.0);

		const FEonformGridDomain Domain = FEonformGridDomain::Make(
			FIntPoint(11, 11),
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
		FEonformScalarField Height = MakeCryosphereField(Domain, EonformTerrainFieldNames::Height, 0.0f);

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

	FEonformTerrainRecipe MakeCryosphereRecipe()
	{
		FEonformTerrainRecipe Recipe;

		FEonformTerrainNode Source;
		Source.Id = FGuid(801, 1, 1, 1);
		Source.Type = EonformTerrainNodeTypes::SourceDataset;

		// Deliberately cold, snowy conditions. This test is about the transition
		// from persistent snow to firn/ice, not whether a marginal climate happens
		// to cross the snow threshold on a particular synthetic ridge.
		FEonformTerrainNode Snow;
		Snow.Id = FGuid(802, 2, 2, 2);
		Snow.Type = EonformTerrainNodeTypes::Snow;
		Snow.NumericParameters.Add(TEXT("BaseTemperatureC"), -5.0);
		Snow.NumericParameters.Add(TEXT("LapseRateCPerKm"), 6.0);
		Snow.NumericParameters.Add(TEXT("SnowTemperatureC"), 2.0);
		Snow.NumericParameters.Add(TEXT("TemperatureTransitionC"), 4.0);
		Snow.NumericParameters.Add(TEXT("Precipitation"), 1.0);
		Snow.NumericParameters.Add(TEXT("MaxDepthMeters"), 8.0);
		Snow.NumericParameters.Add(TEXT("AccumulationSlopeDegrees"), 70.0);
		Snow.NumericParameters.Add(TEXT("MaxStableSlopeDegrees"), 85.0);
		Snow.NumericParameters.Add(TEXT("ShelterStrength"), 0.2);
		Snow.BoolParameters.Add(TEXT("AffectHeight"), true);

		FEonformTerrainNode Snowfield;
		Snowfield.Id = FGuid(803, 3, 3, 3);
		Snowfield.Type = EonformTerrainNodeTypes::Snowfield;
		Snowfield.NumericParameters.Add(TEXT("Iterations"), 4.0);
		Snowfield.NumericParameters.Add(TEXT("TransportStrength"), 0.0);
		Snowfield.NumericParameters.Add(TEXT("Compaction"), 0.0);
		Snowfield.NumericParameters.Add(TEXT("MeltRateMetersPerC"), 0.0);
		Snowfield.BoolParameters.Add(TEXT("AffectHeight"), true);

		FEonformTerrainNode Glacier;
		Glacier.Id = FGuid(804, 4, 4, 4);
		Glacier.Type = EonformTerrainNodeTypes::Glacier;
		Glacier.NumericParameters.Add(TEXT("Iterations"), 4.0);
		Glacier.NumericParameters.Add(TEXT("FirnDepthMeters"), 0.1);
		Glacier.NumericParameters.Add(TEXT("IceCompaction"), 1.0);
		Glacier.NumericParameters.Add(TEXT("FlowStrength"), 0.0);
		Glacier.NumericParameters.Add(TEXT("ValleyPreference"), 0.0);
		Glacier.NumericParameters.Add(TEXT("MeltRateMetersPerC"), 0.0);
		Glacier.NumericParameters.Add(TEXT("ErosionStrength"), 0.05);
		Glacier.BoolParameters.Add(TEXT("AffectHeight"), true);

		FEonformTerrainConnection A;
		A.FromNode = Source.Id;
		A.FromOutput = TEXT("Terrain");
		A.ToNode = Snow.Id;
		A.ToInput = TEXT("Terrain");

		FEonformTerrainConnection B;
		B.FromNode = Snow.Id;
		B.FromOutput = TEXT("Out");
		B.ToNode = Snowfield.Id;
		B.ToInput = TEXT("Terrain");

		FEonformTerrainConnection C;
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
	FEonformCryosphereChainTest,
	"Eonform.Core.Graph.CryosphereChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformCryosphereChainTest::RunTest(const FString& Parameters)
{
	FEonformTerrainNodeDescriptor SnowDescriptor;
	FEonformTerrainNodeDescriptor SnowfieldDescriptor;
	FEonformTerrainNodeDescriptor GlacierDescriptor;
	TestTrue(TEXT("Snow descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Snow, SnowDescriptor));
	TestTrue(TEXT("Snowfield descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Snowfield, SnowfieldDescriptor));
	TestTrue(TEXT("Glacier descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::Glacier, GlacierDescriptor));
	TestEqual(TEXT("Snow parameter contract"), SnowDescriptor.Parameters.Num(), 10);
	TestEqual(TEXT("Snowfield parameter contract"), SnowfieldDescriptor.Parameters.Num(), 8);
	TestEqual(TEXT("Glacier parameter contract"), GlacierDescriptor.Parameters.Num(), 13);

	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(MakeCryosphereRecipe(), MakeCryosphereContext());
	TestTrue(TEXT("Cryosphere graph evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FEonformScalarField* Glacier = Result.Dataset.FindScalarField(TEXT("Glacier"));
	const FEonformScalarField* IceDepth = Result.Dataset.FindScalarField(TEXT("IceDepth"));
	const FEonformScalarField* GlacialErosion = Result.Dataset.FindScalarField(TEXT("GlacialErosion"));
	const FEonformScalarField* IceFlow = Result.Dataset.FindScalarField(TEXT("IceFlow"));
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
