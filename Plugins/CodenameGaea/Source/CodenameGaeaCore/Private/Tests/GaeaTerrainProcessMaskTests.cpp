#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaTerrainProcessMasksPipelineTest,
	"CodenameGaea.Core.Graph.ProceduralTerrainContextProcessMasksHydraulic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaTerrainProcessMasksPipelineTest::RunTest(const FString& Parameters)
{
	FGaeaTerrainRecipe Recipe;

	FGaeaTerrainNode Source;
	Source.Id = FGuid(31, 32, 33, 34);
	Source.Type = GaeaTerrainNodeTypes::ProceduralTerrain;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 84);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

	FGaeaTerrainNode ContextNode;
	ContextNode.Id = FGuid(35, 36, 37, 38);
	ContextNode.Type = GaeaTerrainNodeTypes::TerrainContext;

	FGaeaTerrainNode MasksNode;
	MasksNode.Id = FGuid(39, 40, 41, 42);
	MasksNode.Type = GaeaTerrainNodeTypes::ProcessMasks;

	FGaeaTerrainNode Erosion;
	Erosion.Id = FGuid(43, 44, 45, 46);
	Erosion.Type = GaeaTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

	Recipe.Nodes = { Source, ContextNode, MasksNode, Erosion };

	auto AddConnection = [&Recipe](const FGuid& From, const FGuid& To)
	{
		FGaeaTerrainConnection Connection;
		Connection.FromNode = From;
		Connection.FromOutput = TEXT("Terrain");
		Connection.ToNode = To;
		Connection.ToInput = TEXT("Terrain");
		Recipe.Connections.Add(Connection);
	};

	AddConnection(Source.Id, ContextNode.Id);
	AddConnection(ContextNode.Id, MasksNode.Id);
	AddConnection(MasksNode.Id, Erosion.Id);
	Recipe.OutputNode = Erosion.Id;

	FGaeaTerrainEvaluationContext EmptyContext;
	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, EmptyContext);
	TestTrue(TEXT("Process mask pipeline evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("Process mask pipeline preserves height scale"), Result.HeightScale, 6400.0f);

	const FName ExpectedFields[] =
	{
		GaeaTerrainFieldNames::Height,
		GaeaTerrainFieldNames::Elevation,
		GaeaTerrainFieldNames::SlopeDegrees,
		GaeaTerrainFieldNames::Concavity,
		GaeaTerrainFieldNames::Convexity,
		GaeaTerrainFieldNames::Mountain,
		GaeaTerrainFieldNames::Foothill,
		GaeaTerrainFieldNames::Plains,
		GaeaTerrainFieldNames::Thermal,
		GaeaTerrainFieldNames::Rainfall,
		GaeaTerrainFieldNames::HydraulicErosion,
		GaeaTerrainFieldNames::Deposition,
		GaeaTerrainFieldNames::Evaporation,
		GaeaTerrainFieldNames::Wear,
		GaeaTerrainFieldNames::Deposits,
		GaeaTerrainFieldNames::Flow
	};

	for (const FName FieldName : ExpectedFields)
	{
		TestTrue(
			*FString::Printf(TEXT("Output has %s"), *FieldName.ToString()),
			Result.Dataset.HasScalarField(FieldName));
	}

	FGaeaTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Process Masks descriptor exists"), FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::ProcessMasks, Descriptor));
	TestEqual(TEXT("Process Masks has one input"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Process Masks has one output"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Process Masks exposes five parameters"), Descriptor.Parameters.Num(), 5);

	return true;
}

#endif
