#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaSimulateFoundationNodesTest,
	"CodenameGaea.Core.Graph.SimulateFoundationChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSimulateFoundationNodesTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Types =
	{
		GaeaTerrainNodeTypes::HydroFix,
		GaeaTerrainNodeTypes::Rivers,
		GaeaTerrainNodeTypes::Sediments,
		GaeaTerrainNodeTypes::Debris,
		GaeaTerrainNodeTypes::Scree
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
	Source.Id = FGuid(0x10000001, 0x10000002, 0x10000003, 0x10000004);
	Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 65);
	Source.NumericParameters.Add(TEXT("WorldSize"), 100000.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 8000.0);
	Source.IntegerParameters.Add(TEXT("Seed"), 4242);

	FGaeaTerrainNode HydroFix;
	HydroFix.Id = FGuid(0x20000001, 0x20000002, 0x20000003, 0x20000004);
	HydroFix.Type = GaeaTerrainNodeTypes::HydroFix;
	HydroFix.NumericParameters.Add(TEXT("Downcutting"), 0.4);

	FGaeaTerrainNode Rivers;
	Rivers.Id = FGuid(0x30000001, 0x30000002, 0x30000003, 0x30000004);
	Rivers.Type = GaeaTerrainNodeTypes::Rivers;
	Rivers.NumericParameters.Add(TEXT("Water"), 0.65);
	Rivers.NumericParameters.Add(TEXT("Width"), 0.35);
	Rivers.NumericParameters.Add(TEXT("Depth"), 0.45);
	Rivers.NumericParameters.Add(TEXT("Downcutting"), 0.45);
	Rivers.IntegerParameters.Add(TEXT("Headwaters"), 12);

	FGaeaTerrainNode Sediments;
	Sediments.Id = FGuid(0x40000001, 0x40000002, 0x40000003, 0x40000004);
	Sediments.Type = GaeaTerrainNodeTypes::Sediments;
	Sediments.IntegerParameters.Add(TEXT("Passes"), 2);

	FGaeaTerrainNode Debris;
	Debris.Id = FGuid(0x50000001, 0x50000002, 0x50000003, 0x50000004);
	Debris.Type = GaeaTerrainNodeTypes::Debris;
	Debris.IntegerParameters.Add(TEXT("Seed"), 55);

	FGaeaTerrainNode Scree;
	Scree.Id = FGuid(0x60000001, 0x60000002, 0x60000003, 0x60000004);
	Scree.Type = GaeaTerrainNodeTypes::Scree;
	Scree.IntegerParameters.Add(TEXT("Seed"), 66);

	Recipe.Nodes = { Source, HydroFix, Rivers, Sediments, Debris, Scree };
	auto Connect = [&Recipe](const FGuid& From, const FGuid& To)
	{
		FGaeaTerrainConnection C;
		C.FromNode = From;
		C.FromOutput = TEXT("Out");
		C.ToNode = To;
		C.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(C);
	};
	Connect(Source.Id, HydroFix.Id);
	Connect(HydroFix.Id, Rivers.Id);
	Connect(Rivers.Id, Sediments.Id);
	Connect(Sediments.Id, Debris.Id);
	Connect(Debris.Id, Scree.Id);
	Recipe.OutputNode = Scree.Id;

	FString ValidationError;
	TestTrue(TEXT("Physical Simulate chain validates"), Recipe.Validate(&ValidationError));
	if (!ValidationError.IsEmpty()) AddError(ValidationError);

	FGaeaTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FGaeaTerrainPhysicalMetrics(10000.0, 10000.0, 2000.0, 0.0);
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
	TestTrue(TEXT("Physical Simulate chain evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	const FGaeaScalarField* River = Result.Dataset.FindScalarField(TEXT("River"));
	const FGaeaScalarField* RiverDepth = Result.Dataset.FindScalarField(TEXT("RiverDepth"));
	const FGaeaScalarField* Deposits = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Deposits);
	const FGaeaScalarField* DebrisField = Result.Dataset.FindScalarField(TEXT("Debris"));
	const FGaeaScalarField* ScreeField = Result.Dataset.FindScalarField(TEXT("Scree"));

	TestNotNull(TEXT("Final Height exists"), Height);
	TestNotNull(TEXT("River field persists downstream"), River);
	TestNotNull(TEXT("Physical RiverDepth field persists downstream"), RiverDepth);
	TestNotNull(TEXT("Sediment deposits persist downstream"), Deposits);
	TestNotNull(TEXT("Debris field persists downstream"), DebrisField);
	TestNotNull(TEXT("Scree field persists downstream"), ScreeField);
	if (!Height || !River || !RiverDepth || !Deposits || !DebrisField || !ScreeField) return false;

	TestEqual(TEXT("RiverDepth uses metres"), RiverDepth->Descriptor.Unit, EGaeaFieldUnit::Meters);

	float RiverMaximum = 0.0f;
	float DepthMaximum = 0.0f;
	bool bFinite = true;
	for (int32 Y = 0; Y < Height->Domain.Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
		{
			bFinite &= FMath::IsFinite(Height->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(River->AtInterior(X, Y));
			bFinite &= FMath::IsFinite(RiverDepth->AtInterior(X, Y));
			RiverMaximum = FMath::Max(RiverMaximum, River->AtInterior(X, Y));
			DepthMaximum = FMath::Max(DepthMaximum, RiverDepth->AtInterior(X, Y));
		}
	}

	TestTrue(TEXT("Simulate chain stays finite"), bFinite);
	TestTrue(TEXT("Rivers extracts a non-empty network"), RiverMaximum > 0.0f);
	TestTrue(TEXT("Rivers produces physical channel depth"), DepthMaximum > 0.0f);
	return true;
}

#endif
