#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainGraphAsset.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainGraphAssetModelTest,
	"Eonform.Runtime.GraphAsset.Model",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainGraphAssetModelTest::RunTest(const FString& Parameters)
{
	UEonformTerrainGraphAsset* Asset = NewObject<UEonformTerrainGraphAsset>();
	TestNotNull(TEXT("Graph asset can be created"), Asset);
	if (!Asset)
	{
		return false;
	}

	FEonformTerrainNode Source;
	Source.Id = FGuid(1, 2, 3, 4);
	Source.Type = EonformTerrainNodeTypes::SourceDataset;

	FEonformTerrainNode Erosion;
	Erosion.Id = FGuid(5, 6, 7, 8);
	Erosion.Type = EonformTerrainNodeTypes::HydraulicErosion;
	Erosion.IntegerParameters.Add(TEXT("Iterations"), 17);

	Asset->Recipe.Nodes = { Source, Erosion };
	FEonformTerrainConnection Connection;
	Connection.FromNode = Source.Id;
	Connection.FromOutput = TEXT("Terrain");
	Connection.ToNode = Erosion.Id;
	Connection.ToInput = TEXT("Terrain");
	Asset->Recipe.Connections.Add(Connection);
	Asset->Recipe.OutputNode = Erosion.Id;

	Asset->SetLayout(Source.Id, FVector2D(100.0, 200.0));
	Asset->SetLayout(Erosion.Id, FVector2D(450.0, 200.0));
	Asset->SetLayout(Erosion.Id, FVector2D(500.0, 240.0));

	FString Error;
	TestTrue(TEXT("Stored runtime recipe validates"), Asset->Recipe.Validate(&Error));
	TestEqual(TEXT("Stored node count"), Asset->Recipe.Nodes.Num(), 2);
	TestEqual(TEXT("Layout has one entry per node"), Asset->NodeLayout.Num(), 2);

	const FEonformTerrainNodeLayout* ErosionLayout = Asset->FindLayout(Erosion.Id);
	TestNotNull(TEXT("Erosion layout is found"), ErosionLayout);
	if (ErosionLayout)
	{
		TestEqual(TEXT("Updated erosion X position"), ErosionLayout->Position.X, 500.0);
		TestEqual(TEXT("Updated erosion Y position"), ErosionLayout->Position.Y, 240.0);
	}

	const FEonformTerrainNode* StoredErosion = Asset->Recipe.FindNode(Erosion.Id);
	TestNotNull(TEXT("Stored erosion node is found"), StoredErosion);
	if (StoredErosion)
	{
		TestEqual(TEXT("Stored erosion parameter survives"), StoredErosion->GetInteger(TEXT("Iterations"), 0), static_cast<int64>(17));
	}

	return true;
}

#endif
