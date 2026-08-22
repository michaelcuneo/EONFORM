#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaSimulateEvolutionNodesTest,
	"CodenameGaea.Core.Graph.SimulateEvolutionChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSimulateEvolutionNodesTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Types =
	{
		GaeaTerrainNodeTypes::EasyErosion,
		GaeaTerrainNodeTypes::Erosion2,
		GaeaTerrainNodeTypes::Thermal2,
		GaeaTerrainNodeTypes::Crumble,
		GaeaTerrainNodeTypes::Hillify
	};
	for (const FName Type : Types)
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		TestTrue(*FString::Printf(TEXT("%s descriptor exists"), *Type.ToString()), FGaeaTerrainNodeDescriptorRegistry::Get(Type, Descriptor));
		TestEqual(*FString::Printf(TEXT("%s is Simulate"), *Type.ToString()), Descriptor.Category, FString(TEXT("Simulate")));
		TestTrue(*FString::Printf(TEXT("%s evaluator exists"), *Type.ToString()), FGaeaTerrainNodeRegistry::IsRegistered(Type));
	}

	FGaeaTerrainRecipe Recipe;
	FGaeaTerrainNode Source;
	Source.Id = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 65);
	Source.NumericParameters.Add(TEXT("WorldSize"), 100000.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 8000.0);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);

	FGaeaTerrainNode Easy;
	Easy.Id = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
	Easy.Type = GaeaTerrainNodeTypes::EasyErosion;
	Easy.NameParameters.Add(TEXT("Style"), TEXT("Alpine"));
	Easy.NumericParameters.Add(TEXT("Influence"), 0.6);
	Easy.IntegerParameters.Add(TEXT("Seed"), 73);

	FGaeaTerrainNode Erosion2;
	Erosion2.Id = FGuid(0x73000001, 0x73000002, 0x73000003, 0x73000004);
	Erosion2.Type = GaeaTerrainNodeTypes::Erosion2;
	Erosion2.IntegerParameters.Add(TEXT("Duration"), 8);
	Erosion2.NumericParameters.Add(TEXT("Downcutting"), 0.45);
	Erosion2.NumericParameters.Add(TEXT("SuspendedLoad"), 0.55);
	Erosion2.NumericParameters.Add(TEXT("BedLoad"), 0.4);
	Erosion2.NumericParameters.Add(TEXT("CoarseSediments"), 0.25);
	Erosion2.BoolParameters.Add(TEXT("EnableOrographic"), true);
	Erosion2.NumericParameters.Add(TEXT("DirectionalPrecipitation"), 0.65);
	Erosion2.NumericParameters.Add(TEXT("Direction"), 45.0);
	Erosion2.NumericParameters.Add(TEXT("RainShadow"), 0.25);

	FGaeaTerrainNode Thermal2;
	Thermal2.Id = FGuid(0x74000001, 0x74000002, 0x74000003, 0x74000004);
	Thermal2.Type = GaeaTerrainNodeTypes::Thermal2;
	Thermal2.IntegerParameters.Add(TEXT("Duration"), 4);
	Thermal2.NumericParameters.Add(TEXT("Strength"), 0.35);
	Thermal2.NumericParameters.Add(TEXT("FeatureScale"), 80.0);

	FGaeaTerrainNode Crumble;
	Crumble.Id = FGuid(0x75000001, 0x75000002, 0x75000003, 0x75000004);
	Crumble.Type = GaeaTerrainNodeTypes::Crumble;
	Crumble.IntegerParameters.Add(TEXT("Duration"), 3);
	Crumble.NumericParameters.Add(TEXT("Strength"), 0.3);
	Crumble.NumericParameters.Add(TEXT("Coverage"), 0.55);
	Crumble.NameParameters.Add(TEXT("Direction"), TEXT("E"));

	FGaeaTerrainNode Hillify;
	Hillify.Id = FGuid(0x76000001, 0x76000002, 0x76000003, 0x76000004);
	Hillify.Type = GaeaTerrainNodeTypes::Hillify;
	Hillify.NumericParameters.Add(TEXT("Coverage"), 0.55);
	Hillify.NameParameters.Add(TEXT("Creep"), TEXT("Moderate"));
	Hillify.NameParameters.Add(TEXT("Surface"), TEXT("Eroded"));
	Hillify.IntegerParameters.Add(TEXT("Seed"), 93);

	Recipe.Nodes = { Source, Easy, Erosion2, Thermal2, Crumble, Hillify };
	auto Connect = [&Recipe](const FGuid& From, const FGuid& To)
	{
		FGaeaTerrainConnection C;
		C.FromNode = From;
		C.FromOutput = TEXT("Out");
		C.ToNode = To;
		C.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(C);
	};
	Connect(Source.Id, Easy.Id);
	Connect(Easy.Id, Erosion2.Id);
	Connect(Erosion2.Id, Thermal2.Id);
	Connect(Thermal2.Id, Crumble.Id);
	Connect(Crumble.Id, Hillify.Id);
	Recipe.OutputNode = Hillify.Id;

	FString ValidationError;
	TestTrue(TEXT("Evolution Simulate chain validates"), Recipe.Validate(&ValidationError));
	if (!ValidationError.IsEmpty()) AddError(ValidationError);

	FGaeaTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(25000.0, 18000.0, 3200.0, 0.0);
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Evolution Simulate chain evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* Wear = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Wear);
	const FGaeaScalarField* Deposits = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Deposits);
	const FGaeaScalarField* Flow = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Flow);
	const FGaeaScalarField* Talus = Result.Dataset.FindScalarField(TEXT("Talus"));
	const FGaeaScalarField* CrumbleField = Result.Dataset.FindScalarField(TEXT("Crumble"));
	const FGaeaScalarField* HillifyField = Result.Dataset.FindScalarField(TEXT("Hillify"));

	TestNotNull(TEXT("Final Height exists"), Height);
	TestNotNull(TEXT("Hydraulic Wear persists"), Wear);
	TestNotNull(TEXT("Hydraulic Deposits persist"), Deposits);
	TestNotNull(TEXT("Hydraulic Flow persists"), Flow);
	TestNotNull(TEXT("Thermal2 Talus persists"), Talus);
	TestNotNull(TEXT("Crumble field persists"), CrumbleField);
	TestNotNull(TEXT("Hillify field persists"), HillifyField);
	if (!Height || !Wear || !Deposits || !Flow || !Talus || !CrumbleField || !HillifyField) return false;

	bool bFinite = true;
	float CrumbleMaximum = 0.0f;
	float HillifyMaximum = 0.0f;
	for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
		{
			bFinite &= FMath::IsFinite(Height->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(Wear->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(Deposits->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(Flow->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(Talus->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(CrumbleField->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(HillifyField->AtInterior(X, Y));
			CrumbleMaximum = FMath::Max(CrumbleMaximum, CrumbleField->AtInterior(X, Y));
			HillifyMaximum = FMath::Max(HillifyMaximum, HillifyField->AtInterior(X, Y));
		}
	}
	TestTrue(TEXT("Evolution Simulate chain remains finite"), bFinite);
	TestTrue(TEXT("Crumble produces a non-empty influence field"), CrumbleMaximum > 0.0f);
	TestTrue(TEXT("Hillify produces a non-empty influence field"), HillifyMaximum > 0.0f);
	return true;
}

#endif
