#include "SGaeaTerrainGraphPanel.h"

#include "Async/Async.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainPhysicalMetrics.h"
#include "Modules/ModuleManager.h"

namespace
{
	const FName FinalTerrainSource(TEXT("CodenameGaeaGraph"));
	const FName InspectionTerrainSource(TEXT("CodenameGaeaInspection"));

	bool PrepareEvaluationContext(
		const FGaeaTerrainRecipe& Recipe,
		FGaeaTerrainEvaluationContext& OutContext,
		FString& OutError)
	{
		OutContext = FGaeaTerrainEvaluationContext();
		OutContext.PhysicalMetrics = FGaeaTerrainPhysicalContext::GetActive();

		const bool bUsesExternalSource = Recipe.Nodes.ContainsByPredicate([](const FGaeaTerrainNode& Node)
		{
			return Node.Type == GaeaTerrainNodeTypes::SourceDataset;
		});
		if (bUsesExternalSource)
		{
			FGaeaTerrainDatasetSnapshot SourceSnapshot;
			if (!FGaeaTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot) || !SourceSnapshot.IsValid())
			{
				OutError = TEXT("This graph uses Source Dataset, but no LegacyTerrainGenerator dataset is available.");
				return false;
			}
			OutContext.SourceDataset = SourceSnapshot.Dataset;
			OutContext.HeightScale = SourceSnapshot.Metadata.HeightScale;
		}

		// File decoding itself is safe to do off-thread, but module loading is not.
		const bool bUsesFileNode = Recipe.Nodes.ContainsByPredicate([](const FGaeaTerrainNode& Node)
		{
			return Node.Type == GaeaTerrainNodeTypes::File;
		});
		if (bUsesFileNode && !FModuleManager::Get().IsModuleLoaded(TEXT("ImageWrapper")))
		{
			if (!FModuleManager::Get().LoadModule(TEXT("ImageWrapper")))
			{
				OutError = TEXT("ImageWrapper could not be loaded for the File terrain node.");
				return false;
			}
		}

		OutError.Reset();
		return true;
	}
}

void SGaeaTerrainGraphPanel::RequestFinalEvaluationAsync()
{
	++GraphEvaluationGeneration;
	bFinalEvaluationPending = true;
	if (!bAutoPreviewEvaluating)
	{
		StartNextAsyncEvaluation();
	}
}

void SGaeaTerrainGraphPanel::RequestInspectionEvaluationAsync(const FGuid& NodeId)
{
	if (!NodeId.IsValid()) return;

	++InspectionEvaluationGeneration;
	PendingInspectionNodeId = NodeId;
	if (!bAutoPreviewEvaluating)
	{
		StartNextAsyncEvaluation();
	}
}

void SGaeaTerrainGraphPanel::ClearInspectionPreview()
{
	++InspectionEvaluationGeneration;
	PendingInspectionNodeId.Invalidate();
	FGaeaTerrainDatasetRegistry::Remove(InspectionTerrainSource);
	OnEvaluated.ExecuteIfBound();
}

void SGaeaTerrainGraphPanel::StartNextAsyncEvaluation()
{
	if (bAutoPreviewEvaluating) return;

	const bool bEvaluateFinal = bFinalEvaluationPending;
	FGuid InspectionNodeId;
	if (!bEvaluateFinal)
	{
		InspectionNodeId = PendingInspectionNodeId;
		if (!InspectionNodeId.IsValid()) return;
	}

	FGaeaTerrainRecipe Recipe;
	FString Error;
	if (!BuildRecipeFromEditorGraph(Recipe, Error))
	{
		if (bEvaluateFinal) bFinalEvaluationPending = false;
		else PendingInspectionNodeId.Invalidate();
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *Error));
		return;
	}

	if (!bEvaluateFinal)
	{
		Recipe.OutputNode = InspectionNodeId;
		if (!Recipe.Validate(&Error))
		{
			PendingInspectionNodeId.Invalidate();
			StatusText = FText::FromString(FString::Printf(TEXT("Inspection is invalid: %s"), *Error));
			return;
		}
	}

	FGaeaTerrainEvaluationContext Context;
	if (!PrepareEvaluationContext(Recipe, Context, Error))
	{
		if (bEvaluateFinal) bFinalEvaluationPending = false;
		else PendingInspectionNodeId.Invalidate();
		StatusText = FText::FromString(Error);
		return;
	}

	const uint64 CapturedGraphGeneration = GraphEvaluationGeneration;
	const uint64 CapturedInspectionGeneration = InspectionEvaluationGeneration;
	if (bEvaluateFinal)
	{
		bFinalEvaluationPending = false;
		StatusText = FText::FromString(TEXT("Analysing terrain in background..."));
	}
	else
	{
		PendingInspectionNodeId.Invalidate();
		StatusText = FText::FromString(TEXT("Inspecting selected node in background..."));
	}

	bAutoPreviewEvaluating = true;
	TWeakPtr<SGaeaTerrainGraphPanel> WeakPanel = SharedThis(this);
	Async(EAsyncExecution::ThreadPool,
		[WeakPanel,
		 Recipe = MoveTemp(Recipe),
		 Context = MoveTemp(Context),
		 bEvaluateFinal,
		 InspectionNodeId,
		 CapturedGraphGeneration,
		 CapturedInspectionGeneration]() mutable
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
					EvaluationError = FString::Printf(TEXT("Hydrology analysis failed: %s"), *HydrologyError);
				}
			}

			AsyncTask(ENamedThreads::GameThread,
				[WeakPanel,
				 bEvaluateFinal,
				 InspectionNodeId,
				 CapturedGraphGeneration,
				 CapturedInspectionGeneration,
				 Result = MoveTemp(Result),
				 EvaluationError = MoveTemp(EvaluationError)]() mutable
				{
					const TSharedPtr<SGaeaTerrainGraphPanel> Panel = WeakPanel.Pin();
					if (!Panel.IsValid()) return;

					Panel->bAutoPreviewEvaluating = false;

					const bool bStale = bEvaluateFinal
						? CapturedGraphGeneration != Panel->GraphEvaluationGeneration
						: CapturedInspectionGeneration != Panel->InspectionEvaluationGeneration;
					if (bStale)
					{
						Panel->StartNextAsyncEvaluation();
						return;
					}

					if (!EvaluationError.IsEmpty() || !Result.bSuccess)
					{
						Panel->StatusText = FText::FromString(FString::Printf(
							TEXT("%s failed: %s"),
							bEvaluateFinal ? TEXT("Terrain analysis") : TEXT("Node inspection"),
							EvaluationError.IsEmpty() ? *Result.Error : *EvaluationError));
						Panel->StartNextAsyncEvaluation();
						return;
					}

					FGaeaTerrainDatasetMetadata Metadata;
					Metadata.HeightScale = Result.HeightScale;
					const int32 FieldCount = Result.Dataset.NumScalarFields();
					const uint32 RecipeHash = Result.RecipeHash;

					const FName SourceId = bEvaluateFinal ? FinalTerrainSource : InspectionTerrainSource;
					const uint64 Revision = FGaeaTerrainDatasetRegistry::Publish(
						SourceId,
						MoveTemp(Result.Dataset),
						Metadata);
					if (Revision == 0)
					{
						Panel->StatusText = FText::FromString(TEXT("Terrain evaluated successfully, but publishing the snapshot failed."));
						Panel->StartNextAsyncEvaluation();
						return;
					}

					if (bEvaluateFinal)
					{
						// Final Terrain Output is permanent and authoritative. Inspection data uses
						// a different source and can never remove/replace this snapshot.
						Panel->StatusText = FText::FromString(FString::Printf(
							TEXT("Analysis %08X -> revision %llu (%d fields, height scale %.1f)."),
							RecipeHash,
							static_cast<unsigned long long>(Revision),
							FieldCount,
							Metadata.HeightScale));
					}
					else
					{
						Panel->StatusText = FText::FromString(FString::Printf(
							TEXT("Inspected node %s -> revision %llu (%d fields)."),
							*InspectionNodeId.ToString(EGuidFormats::Short),
							static_cast<unsigned long long>(Revision),
							FieldCount));
					}

					Panel->OnEvaluated.ExecuteIfBound();
					Panel->StartNextAsyncEvaluation();
				});
		});
}
