#include "SGaeaTerrainGraphPanel.h"

#include "Async/Async.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainPhysicalMetrics.h"

void SGaeaTerrainGraphPanel::RequestAutoPreviewEvaluation()
{
	++AutoPreviewRequestSerial;
	bAutoPreviewRestartPending = true;
	if (!bAutoPreviewEvaluating)
	{
		StartAutoPreviewEvaluation();
	}
}

void SGaeaTerrainGraphPanel::StartAutoPreviewEvaluation()
{
	if (bAutoPreviewEvaluating || !bAutoPreviewRestartPending)
	{
		return;
	}

	FGaeaTerrainRecipe Recipe;
	FString RecipeError;
	if (!BuildRecipeFromEditorGraph(Recipe, RecipeError))
	{
		bAutoPreviewRestartPending = false;
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *RecipeError));
		return;
	}

	FGaeaTerrainEvaluationContext Context;
	Context.PhysicalMetrics = FGaeaTerrainPhysicalContext::GetActive();

	const bool bUsesExternalSource = Recipe.Nodes.ContainsByPredicate([](const FGaeaTerrainNode& Node)
	{
		return Node.Type == GaeaTerrainNodeTypes::SourceDataset;
	});
	if (bUsesExternalSource)
	{
		FGaeaTerrainDatasetSnapshot SourceSnapshot;
		if (!FGaeaTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot) || !SourceSnapshot.IsValid())
		{
			bAutoPreviewRestartPending = false;
			StatusText = FText::FromString(TEXT("This graph uses Source Dataset, but no LegacyTerrainGenerator dataset is available."));
			return;
		}
		Context.SourceDataset = SourceSnapshot.Dataset;
		Context.HeightScale = SourceSnapshot.Metadata.HeightScale;
	}

	const uint64 RequestSerial = AutoPreviewRequestSerial;
	bAutoPreviewRestartPending = false;
	bAutoPreviewEvaluating = true;
	StatusText = FText::FromString(TEXT("Updating terrain preview in background..."));

	TWeakPtr<SGaeaTerrainGraphPanel> WeakPanel = SharedThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakPanel, Recipe = MoveTemp(Recipe), Context = MoveTemp(Context), RequestSerial]() mutable
	{
		FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
		FString EvaluationError;
		if (!Result.bSuccess)
		{
			EvaluationError = Result.Error;
		}
		else
		{
			FString HydrologyError;
			if (!FGaeaTerrainDerivedData::EnsureHydrology(
				Result.Dataset,
				Result.HeightScale,
				Context.PhysicalMetrics,
				&HydrologyError))
			{
				EvaluationError = FString::Printf(TEXT("EONFORM hydrology analysis failed: %s"), *HydrologyError);
			}
		}

		AsyncTask(ENamedThreads::GameThread, [WeakPanel, RequestSerial, Result = MoveTemp(Result), EvaluationError = MoveTemp(EvaluationError)]() mutable
		{
			const TSharedPtr<SGaeaTerrainGraphPanel> Panel = WeakPanel.Pin();
			if (!Panel.IsValid())
			{
				return;
			}

			Panel->bAutoPreviewEvaluating = false;

			// A newer semantic edit arrived while this worker was running. Never publish
			// stale terrain over the latest graph state; immediately start the newest job.
			if (RequestSerial != Panel->AutoPreviewRequestSerial)
			{
				Panel->bAutoPreviewRestartPending = true;
				Panel->StartAutoPreviewEvaluation();
				return;
			}

			if (!EvaluationError.IsEmpty() || !Result.bSuccess)
			{
				Panel->StatusText = FText::FromString(FString::Printf(
					TEXT("Terrain preview failed: %s"),
					EvaluationError.IsEmpty() ? *Result.Error : *EvaluationError));
			}
			else
			{
				const int32 FieldCount = Result.Dataset.NumScalarFields();
				const uint32 RecipeHash = Result.RecipeHash;
				FGaeaTerrainDatasetMetadata Metadata;
				Metadata.HeightScale = Result.HeightScale;

				const uint64 Revision = FGaeaTerrainDatasetRegistry::Publish(
					TEXT("CodenameGaeaGraph"),
					MoveTemp(Result.Dataset),
					Metadata);

				if (Revision == 0)
				{
					Panel->StatusText = FText::FromString(TEXT("Terrain preview evaluated, but publishing the result failed."));
				}
				else
				{
					Panel->StatusText = FText::FromString(FString::Printf(
						TEXT("Preview %08X -> revision %llu (%d fields, height scale %.1f)."),
						RecipeHash,
						static_cast<unsigned long long>(Revision),
						FieldCount,
						Metadata.HeightScale));
					Panel->OnEvaluated.ExecuteIfBound();
				}
			}

			if (Panel->bAutoPreviewRestartPending)
			{
				Panel->StartAutoPreviewEvaluation();
			}
		});
	});
}
