#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGaeaSurfaceNodeCoverageTest,
	"CodenameGaea.Core.Graph.SurfaceNodeCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaSurfaceNodeCoverageTest::RunTest(const FString& Parameters)
{
	const TArray<FName> SurfaceTypes =
	{
		GaeaTerrainNodeTypes::Bomber,
		GaeaTerrainNodeTypes::Bulbous,
		GaeaTerrainNodeTypes::Contours,
		GaeaTerrainNodeTypes::Craggy,
		GaeaTerrainNodeTypes::Distress,
		GaeaTerrainNodeTypes::FractalTerraces,
		GaeaTerrainNodeTypes::Grid,
		GaeaTerrainNodeTypes::GroundTexture,
		GaeaTerrainNodeTypes::Outcrops,
		GaeaTerrainNodeTypes::Pockmarks,
		GaeaTerrainNodeTypes::RockNoise,
		GaeaTerrainNodeTypes::Rockscape,
		GaeaTerrainNodeTypes::Roughen,
		GaeaTerrainNodeTypes::Sand,
		GaeaTerrainNodeTypes::Sandstone,
		GaeaTerrainNodeTypes::Shatter,
		GaeaTerrainNodeTypes::Shear,
		GaeaTerrainNodeTypes::Steps,
		GaeaTerrainNodeTypes::Stones,
		GaeaTerrainNodeTypes::Stratify,
		GaeaTerrainNodeTypes::Terrace
	};

	TestEqual(TEXT("Current Surface catalogue count"), SurfaceTypes.Num(), 21);

	for (int32 Index = 0; Index < SurfaceTypes.Num(); ++Index)
	{
		const FName SurfaceType = SurfaceTypes[Index];
		FGaeaTerrainNodeDescriptor Descriptor;
		TestTrue(*FString::Printf(TEXT("%s descriptor exists"), *SurfaceType.ToString()),
			FGaeaTerrainNodeDescriptorRegistry::Get(SurfaceType, Descriptor));
		TestEqual(*FString::Printf(TEXT("%s is in Surface category"), *SurfaceType.ToString()), Descriptor.Category, FString(TEXT("Surface")));
		TestTrue(*FString::Printf(TEXT("%s evaluator exists"), *SurfaceType.ToString()),
			FGaeaTerrainNodeRegistry::IsRegistered(SurfaceType));

		FGaeaTerrainRecipe Recipe;
		FGaeaTerrainNode Source;
		Source.Id = FGuid(0x10000000 + Index, 0x20000000, 0x30000000, 0x40000000);
		Source.Type = GaeaTerrainNodeTypes::PerlinNoise;
		Source.IntegerParameters.Add(TEXT("Resolution"), 33);
		Source.NumericParameters.Add(TEXT("WorldSize"), 100000.0);
		Source.NumericParameters.Add(TEXT("HeightScale"), 8000.0);
		Source.IntegerParameters.Add(TEXT("Seed"), 1000 + Index);

		FGaeaTerrainNode Surface;
		Surface.Id = FGuid(0x50000000 + Index, 0x60000000, 0x70000000, 0x80000000);
		Surface.Type = SurfaceType;
		Surface.NumericParameters.Add(TEXT("Strength"), 0.6);
		Surface.IntegerParameters.Add(TEXT("Seed"), 2000 + Index);

		FGaeaTerrainConnection Connection;
		Connection.FromNode = Source.Id;
		Connection.FromOutput = TEXT("Out");
		Connection.ToNode = Surface.Id;
		Connection.ToInput = TEXT("Terrain");

		Recipe.Nodes.Add(Source);
		Recipe.Nodes.Add(Surface);
		Recipe.Connections.Add(Connection);
		Recipe.OutputNode = Surface.Id;

		FString ValidationError;
		TestTrue(*FString::Printf(TEXT("%s recipe validates"), *SurfaceType.ToString()), Recipe.Validate(&ValidationError));
		if (!ValidationError.IsEmpty()) AddError(ValidationError);

		FGaeaTerrainEvaluationContext Context;
		const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
		TestTrue(*FString::Printf(TEXT("%s evaluates"), *SurfaceType.ToString()), Result.bSuccess);
		if (!Result.bSuccess)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *SurfaceType.ToString(), *Result.Error));
			continue;
		}

		const FGaeaScalarField* Height = Result.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		TestNotNull(*FString::Printf(TEXT("%s publishes Height"), *SurfaceType.ToString()), Height);
		if (!Height || !Height->IsValid()) continue;

		bool bFinite = true;
		for (int32 Y = 0; Y < Height->Domain.Dimensions.Y && bFinite; ++Y)
		{
			for (int32 X = 0; X < Height->Domain.Dimensions.X; ++X)
			{
				if (!FMath::IsFinite(Height->AtInterior(X, Y)))
				{
					bFinite = false;
					break;
				}
			}
		}
		TestTrue(*FString::Printf(TEXT("%s Height is finite"), *SurfaceType.ToString()), bFinite);
	}

	return true;
}

#endif
