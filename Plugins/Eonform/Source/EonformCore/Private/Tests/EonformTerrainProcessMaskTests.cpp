#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainProcessMasksPipelineTest,
	"Eonform.Core.Graph.PerlinNoiseContextProcessMasksHydraulic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainProcessMasksPipelineTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRecipe Recipe;

	FEonformTerrainNode Source;
	Source.Id = FGuid(31, 32, 33, 34);
	Source.Type = EonformTerrainNodeTypes::PerlinNoise;
	Source.IntegerParameters.Add(TEXT("Resolution"), 17);
	Source.IntegerParameters.Add(TEXT("Seed"), 84);
	Source.NumericParameters.Add(TEXT("WorldSize"), 1600.0);
	Source.NumericParameters.Add(TEXT("HeightScale"), 6400.0);
	Source.NumericParameters.Add(TEXT("Frequency"), 0.001);

	FEonformTerrainNode ContextNode;
	ContextNode.Id = FGuid(35, 36, 37, 38);
	ContextNode.Type = EonformTerrainNodeTypes::TerrainContext;

	FEonformTerrainNode MasksNode;
	MasksNode.Id = FGuid(39, 40, 41, 42);
	MasksNode.Type = EonformTerrainNodeTypes::ProcessMasks;

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(43, 44, 45, 46);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 2);

	Recipe.Nodes = { Source, ContextNode, MasksNode, Erosion };

	auto AddConnection = [&Recipe](const FGuid& From, const FGuid& To)
	{
		FEonformTerrainConnection Connection;
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

	FEonformTerrainEvaluationContext EmptyContext;
	const FEonformTerrainEvaluationResult Result = FEonformTerrainEvaluator::Evaluate(Recipe, EmptyContext);
	TestTrue(TEXT("Process mask pipeline evaluates"), Result.bSuccess);
	if (!Result.bSuccess)
	{
		AddError(Result.Error);
		return false;
	}

	TestEqual(TEXT("Process mask pipeline preserves height scale"), Result.HeightScale, 6400.0f);

	const FName ExpectedFields[] =
	{
		EonformTerrainFieldNames::Height,
		EonformTerrainFieldNames::Elevation,
		EonformTerrainFieldNames::SlopeDegrees,
		EonformTerrainFieldNames::Concavity,
		EonformTerrainFieldNames::Convexity,
		EonformTerrainFieldNames::Mountain,
		EonformTerrainFieldNames::Foothill,
		EonformTerrainFieldNames::Plains,
		EonformTerrainFieldNames::Thermal,
		EonformTerrainFieldNames::Rainfall,
		EonformTerrainFieldNames::HydraulicErosion,
		EonformTerrainFieldNames::Deposition,
		EonformTerrainFieldNames::Evaporation,
		EonformTerrainFieldNames::Wear,
		EonformTerrainFieldNames::Deposits,
		EonformTerrainFieldNames::Flow
	};

	for (const FName FieldName : ExpectedFields)
	{
		TestTrue(
			*FString::Printf(TEXT("Output has %s"), *FieldName.ToString()),
			Result.Dataset.HasScalarField(FieldName));
	}

	FEonformTerrainNodeDescriptor Descriptor;
	TestTrue(TEXT("Process Masks descriptor exists"), FEonformTerrainNodeDescriptorRegistry::Get(EonformTerrainNodeTypes::ProcessMasks, Descriptor));
	TestEqual(TEXT("Process Masks has one input"), Descriptor.Inputs.Num(), 1);
	TestEqual(TEXT("Process Masks has one output"), Descriptor.Outputs.Num(), 1);
	TestEqual(TEXT("Process Masks exposes five parameters"), Descriptor.Parameters.Num(), 5);

	return true;
}

#endif
