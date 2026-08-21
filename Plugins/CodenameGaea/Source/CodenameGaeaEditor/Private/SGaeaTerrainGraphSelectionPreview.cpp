#include "SGaeaTerrainGraphPanel.h"

#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"

bool SGaeaTerrainGraphPanel::EvaluateSelectedNodePreview()
{
	UGaeaEditorGraphNode* PreviewNode = SelectedNode.Get();
	if (!PreviewNode || PreviewNode->RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		OnEvaluated.ExecuteIfBound();
		return true;
	}

	FGaeaTerrainRecipe PreviewRecipe;
	FString RecipeError;
	if (!BuildRecipeFromEditorGraph(PreviewRecipe, RecipeError))
	{
		return false;
	}

	PreviewRecipe.OutputNode = PreviewNode->RecipeNodeId;
	if (!PreviewRecipe.Validate(&RecipeError))
	{
		return false;
	}

	const bool bUsesExternalSource = PreviewRecipe.Nodes.ContainsByPredicate([](const FGaeaTerrainNode& Node)
	{
		return Node.Type == GaeaTerrainNodeTypes::SourceDataset;
	});

	FGaeaTerrainEvaluationContext Context;
	if (bUsesExternalSource)
	{
		FGaeaTerrainDatasetSnapshot SourceSnapshot;
		if (!FGaeaTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot) || !SourceSnapshot.IsValid())
		{
			return false;
		}
		Context.SourceDataset = SourceSnapshot.Dataset;
		Context.HeightScale = SourceSnapshot.Metadata.HeightScale;
	}

	FGaeaTerrainEvaluationResult PreviewResult = FGaeaTerrainEvaluator::Evaluate(PreviewRecipe, Context);
	if (!PreviewResult.bSuccess)
	{
		return false;
	}

	FString HydrologyError;
	if (!FGaeaTerrainDerivedData::EnsureHydrology(PreviewResult.Dataset, PreviewResult.HeightScale, &HydrologyError))
	{
		return false;
	}

	FGaeaTerrainDatasetSnapshot FinalSnapshot;
	const bool bHadFinalSnapshot = FGaeaTerrainDatasetRegistry::Get(TEXT("CodenameGaeaGraph"), FinalSnapshot)
		&& FinalSnapshot.IsValid();

	FGaeaTerrainDatasetMetadata PreviewMetadata;
	PreviewMetadata.HeightScale = PreviewResult.HeightScale;

	const uint64 PreviewRevision = FGaeaTerrainDatasetRegistry::Publish(
		TEXT("CodenameGaeaGraph"),
		MoveTemp(PreviewResult.Dataset),
		PreviewMetadata);
	if (PreviewRevision == 0)
	{
		return false;
	}

	// The inspector refresh is synchronous. It copies this selected-node snapshot into
	// its own UI state, including the embedded Dynamic Mesh preview.
	OnEvaluated.ExecuteIfBound();

	// Immediately restore the true Terrain Output snapshot so Generate Terrain can never
	// commit an intermediate inspection node by accident.
	if (bHadFinalSnapshot)
	{
		FGaeaTerrainDatasetRegistry::Publish(
			TEXT("CodenameGaeaGraph"),
			FinalSnapshot.Dataset,
			FinalSnapshot.Metadata);
	}
	else
	{
		FGaeaTerrainDatasetRegistry::Remove(TEXT("CodenameGaeaGraph"));
	}

	return true;
}
