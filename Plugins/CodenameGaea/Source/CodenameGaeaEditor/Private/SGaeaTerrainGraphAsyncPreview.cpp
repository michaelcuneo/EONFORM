#include "SGaeaTerrainGraphPanel.h"

#include "Async/Async.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainPhysicalMetrics.h"

void SGaeaTerrainGraphPanel::RequestAutoPreviewEvaluation()
{
	++AutoPreviewRequestSerial;
	bFinalAutoPreviewPending = true;
	bAutoPreviewRestartPending = true;
	if (!bAutoPreviewEvaluating)
	{
		StartAutoPreviewEvaluation();
	}
}

void SGaeaTerrainGraphPanel::RequestSelectedNodePreview(const FGuid& NodeId)
{
	if (!NodeId.IsValid()) return;

	PendingSelectedPreviewNodeId = NodeId;
	bAutoPreviewRestartPending = true;

	// Do not invalidate a valid final-terrain solve just because the user clicked a
	// different node. If a selected-node preview is already running, however, discard
	// that stale inspection and keep only the newest selected node.
	if (bAutoPreviewEvaluating && ActiveAutoPreviewNodeId.IsValid())
	{
		++AutoPreviewRequestSerial;
	}
	else if (!bAutoPreviewEvaluating)
	{
		++AutoPreviewRequestSerial;
		StartAutoPreviewEvaluation();
	}
}

void SGaeaTerrainGraphPanel::StartAutoPreviewEvaluation()
{
	if (bAutoPreviewEvaluating || !bAutoPreviewRestartPending)
	{
		return;
	}

	const bool bEvaluateFinal = bFinalAutoPreviewPending;
	FGuid PreviewNodeId;
	if (!bEvaluateFinal)
	{
		PreviewNodeId = PendingSelectedPreviewNodeId;
		if (!PreviewNodeId.IsValid())
		{
			bAutoPreviewRestartPending = false;
			return;
		}
	}

	FGaeaTerrainRecipe Recipe;
	FString RecipeError;
	if (!BuildRecipeFromEditorGraph(Recipe, RecipeError))
	{
		bFinalAutoPreviewPending = false;
		PendingSelectedPreviewNodeId.Invalidate();
		bAutoPreviewRestartPending = false;
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *RecipeError));
		return;
	}

	if (!bEvaluateFinal)
	{
		Recipe.OutputNode = PreviewNodeId;
		if (!Recipe.Validate(&RecipeError))
		{
			PendingSelectedPreviewNodeId.Invalidate();
			bAutoPreviewRestartPending = bFinalAutoPreviewPending;
			StatusText = FText::FromString(FString::Printf(TEXT("Selected-node preview is invalid: %s"), *RecipeError));
			if (bAutoPreviewRestartPending) StartAutoPreviewEvaluation();
			return;
		}
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
			if (bEvaluateFinal) bFinalAutoPreviewPending = false;
			else PendingSelectedPreviewNodeId.Invalidate();
			bAutoPreviewRestartPending = bFinalAutoPreviewPending || PendingSelectedPreviewNodeId.IsValid();
			StatusText = FText::FromString(TEXT("This graph uses Source Dataset, but no LegacyTerrainGenerator dataset is available."));
			return;
		}
		Context.SourceDataset = SourceSnapshot.Dataset;
		Context.HeightScale = SourceSnapshot.Metadata.HeightScale;
	}

	const uint64 RequestSerial = AutoPreviewRequestSerial;
	if (bEvaluateFinal)
	{
		bFinalAutoPreviewPending = false;
		ActiveAutoPreviewNodeId.Invalidate();
		StatusText = FText::FromString(TEXT("Updating terrain preview in background..."));
	}
	else
	{
		PendingSelectedPreviewNodeId.Invalidate();
		ActiveAutoPreviewNodeId = PreviewNodeId;
		StatusText = FText::FromString(TEXT("Updating selected-node inspection in background..."));
	}
	bAutoPreviewRestartPending = bFinalAutoPreviewPending || PendingSelectedPreviewNodeId.IsValid();
	bAutoPreviewEvaluating = true;

	TWeakPtr<SGaeaTerrainGraphPanel> WeakPanel = SharedThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakPanel, Recipe = MoveTemp(Recipe), Context = MoveTemp(Context), RequestSerial, PreviewNodeId, bEvaluateFinal]() mutable
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

		AsyncTask(ENamedThreads::GameThread, [WeakPanel, RequestSerial, PreviewNodeId, bEvaluateFinal, Result = MoveTemp(Result), EvaluationError = MoveTemp(EvaluationError)]() mutable
		{
			const TSharedPtr<SGaeaTerrainGraphPanel> Panel = WeakPanel.Pin();
			if (!Panel.IsValid()) return;

			Panel->bAutoPreviewEvaluating = false;
			Panel->ActiveAutoPreviewNodeId.Invalidate();

			// A newer semantic edit or selected-node request superseded this job. Keep
			// pending work intact and immediately continue with the newest state.
			if (RequestSerial != Panel->AutoPreviewRequestSerial)
			{
				Panel->bAutoPreviewRestartPending = Panel->bFinalAutoPreviewPending || Panel->PendingSelectedPreviewNodeId.IsValid();
				if (Panel->bAutoPreviewRestartPending) Panel->StartAutoPreviewEvaluation();
				return;
			}

			if (!EvaluationError.IsEmpty() || !Result.bSuccess)
			{
				Panel->StatusText = FText::FromString(FString::Printf(
					TEXT("%s failed: %s"),
					bEvaluateFinal ? TEXT("Terrain preview") : TEXT("Selected-node inspection"),
					EvaluationError.IsEmpty() ? *Result.Error : *EvaluationError));
			}
			else
			{
				const int32 FieldCount = Result.Dataset.NumScalarFields();
				const uint32 RecipeHash = Result.RecipeHash;
				FGaeaTerrainDatasetMetadata Metadata;
				Metadata.HeightScale = Result.HeightScale;

				if (bEvaluateFinal)
				{
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
				else
				{
					FGaeaTerrainDatasetSnapshot FinalSnapshot;
					const bool bHadFinalSnapshot = FGaeaTerrainDatasetRegistry::Get(TEXT("CodenameGaeaGraph"), FinalSnapshot)
						&& FinalSnapshot.IsValid();

					const uint64 PreviewRevision = FGaeaTerrainDatasetRegistry::Publish(
						TEXT("CodenameGaeaGraph"),
						MoveTemp(Result.Dataset),
						Metadata);

					if (PreviewRevision != 0)
					{
						Panel->StatusText = FText::FromString(FString::Printf(
							TEXT("Inspected node %s (%d fields, height scale %.1f)."),
							*PreviewNodeId.ToString(EGuidFormats::Short),
							FieldCount,
							Metadata.HeightScale));

						// Inspector refresh is synchronous and copies the selected-node snapshot into
						// its own UI state. Restore the authoritative final terrain immediately after.
						Panel->OnEvaluated.ExecuteIfBound();
					}

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
				}
			}

			Panel->bAutoPreviewRestartPending = Panel->bFinalAutoPreviewPending || Panel->PendingSelectedPreviewNodeId.IsValid();
			if (Panel->bAutoPreviewRestartPending)
			{
				Panel->StartAutoPreviewEvaluation();
			}
		});
	});
}
