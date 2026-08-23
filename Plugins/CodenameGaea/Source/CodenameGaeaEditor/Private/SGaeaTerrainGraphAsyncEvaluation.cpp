#include "SGaeaTerrainGraphPanel.h"

#include "Async/Async.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainOutputEditorState.h"
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
		OutContext.CacheContextRevision = FGaeaTerrainPhysicalContext::GetRevision();

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
			OutContext.CacheContextRevision = HashCombineFast(
				GetTypeHash(OutContext.CacheContextRevision),
				GetTypeHash(SourceSnapshot.Revision));
		}

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

	void SetGraphActivity(const TSharedPtr<SGaeaTerrainGraphPanel>& Panel, EGaeaEditorGraphActivity Activity)
	{
		if (Panel.IsValid() && Panel->EditorGraph.IsValid())
		{
			Panel->EditorGraph->SetActivity(Activity);
		}
	}
}

void SGaeaTerrainGraphPanel::RequestFinalEvaluationAsync()
{
	++GraphEvaluationGeneration;
	bFinalEvaluationPending = true;
	FGaeaTerrainOutputEditorState::Get().BeginAnalysis();

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
		if (!InspectionNodeId.IsValid())
		{
			if (EditorGraph.IsValid()) EditorGraph->SetActivity(EGaeaEditorGraphActivity::Idle);
			return;
		}
	}

	FGaeaTerrainRecipe Recipe;
	FString Error;
	if (!BuildRecipeFromEditorGraph(Recipe, Error))
	{
		if (bEvaluateFinal)
		{
			bFinalEvaluationPending = false;
			FGaeaTerrainOutputEditorState::Get().FailAnalysis(Error);
		}
		else
		{
			PendingInspectionNodeId.Invalidate();
		}
		if (EditorGraph.IsValid()) EditorGraph->SetActivity(EGaeaEditorGraphActivity::Idle);
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *Error));
		return;
	}

	if (!bEvaluateFinal)
	{
		Recipe.OutputNode = InspectionNodeId;
		if (!Recipe.Validate(&Error))
		{
			PendingInspectionNodeId.Invalidate();
			if (EditorGraph.IsValid()) EditorGraph->SetActivity(EGaeaEditorGraphActivity::Idle);
			StatusText = FText::FromString(FString::Printf(TEXT("Inspection is invalid: %s"), *Error));
			return;
		}
	}

	FGaeaTerrainEvaluationContext Context;
	if (!PrepareEvaluationContext(Recipe, Context, Error))
	{
		if (bEvaluateFinal)
		{
			bFinalEvaluationPending = false;
			FGaeaTerrainOutputEditorState::Get().FailAnalysis(Error);
		}
		else
		{
			PendingInspectionNodeId.Invalidate();
		}
		if (EditorGraph.IsValid()) EditorGraph->SetActivity(EGaeaEditorGraphActivity::Idle);
		StatusText = FText::FromString(Error);
		return;
	}

	const uint64 CapturedGraphGeneration = GraphEvaluationGeneration;
	const uint64 CapturedInspectionGeneration = InspectionEvaluationGeneration;
	if (bEvaluateFinal)
	{
		bFinalEvaluationPending = false;
		StatusText = FText::FromString(TEXT("Evaluating terrain incrementally in background..."));
	}
	else
	{
		PendingInspectionNodeId.Invalidate();
		StatusText = FText::FromString(TEXT("Inspecting selected node in background..."));
	}

	bAutoPreviewEvaluating = true;
	if (EditorGraph.IsValid()) EditorGraph->SetActivity(EGaeaEditorGraphActivity::Solving);

	TWeakPtr<SGaeaTerrainGraphPanel> WeakPanel = SharedThis(this);
	const TSharedPtr<FGaeaTerrainEvaluationCache, ESPMode::ThreadSafe> CapturedCache = IncrementalEvaluationCache;
	Async(EAsyncExecution::ThreadPool,
		[WeakPanel,
		 CapturedCache,
		 Recipe = MoveTemp(Recipe),
		 Context = MoveTemp(Context),
		 bEvaluateFinal,
		 InspectionNodeId,
		 CapturedGraphGeneration,
		 CapturedInspectionGeneration]() mutable
		{
			FGaeaTerrainEvaluationResult Result = bEvaluateFinal && CapturedCache.IsValid()
				? FGaeaTerrainEvaluator::EvaluateIncremental(Recipe, Context, *CapturedCache)
				: FGaeaTerrainEvaluator::Evaluate(Recipe, Context);
			if (!Result.bSuccess)
			{
				const FString Failure = Result.Error;
				AsyncTask(ENamedThreads::GameThread,
					[WeakPanel, bEvaluateFinal, CapturedGraphGeneration, CapturedInspectionGeneration, Failure]()
					{
						const TSharedPtr<SGaeaTerrainGraphPanel> Panel = WeakPanel.Pin();
						if (!Panel.IsValid()) return;
						Panel->bAutoPreviewEvaluating = false;
						const bool bStale = bEvaluateFinal
							? CapturedGraphGeneration != Panel->GraphEvaluationGeneration
							: CapturedInspectionGeneration != Panel->InspectionEvaluationGeneration;
						if (!bStale)
						{
							if (bEvaluateFinal) FGaeaTerrainOutputEditorState::Get().FailAnalysis(Failure);
							Panel->StatusText = FText::FromString(FString::Printf(
								TEXT("%s failed: %s"),
								bEvaluateFinal ? TEXT("Terrain evaluation") : TEXT("Node inspection"),
								*Failure));
						}
						SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
						Panel->StartNextAsyncEvaluation();
					});
				return;
			}

			FGaeaTerrainDataset BaseDataset = Result.Dataset;
			const float HeightScale = Result.HeightScale;
			const uint32 RecipeHash = Result.RecipeHash;
			const int32 BaseFieldCount = BaseDataset.NumScalarFields();
			const int32 EvaluatedNodeCount = Result.EvaluatedNodeCount;
			const int32 CachedNodeCount = Result.CachedNodeCount;
			const double EvaluationMilliseconds = Result.EvaluationMilliseconds;

			AsyncTask(ENamedThreads::GameThread,
				[WeakPanel,
				 bEvaluateFinal,
				 InspectionNodeId,
				 CapturedGraphGeneration,
				 CapturedInspectionGeneration,
				 Context = MoveTemp(Context),
				 Result = MoveTemp(Result),
				 BaseDataset = MoveTemp(BaseDataset),
				 HeightScale,
				 RecipeHash,
				 BaseFieldCount,
				 EvaluatedNodeCount,
				 CachedNodeCount,
				 EvaluationMilliseconds]() mutable
				{
					const TSharedPtr<SGaeaTerrainGraphPanel> Panel = WeakPanel.Pin();
					if (!Panel.IsValid()) return;

					const bool bStale = bEvaluateFinal
						? CapturedGraphGeneration != Panel->GraphEvaluationGeneration
						: CapturedInspectionGeneration != Panel->InspectionEvaluationGeneration;
					if (bStale)
					{
						Panel->bAutoPreviewEvaluating = false;
						SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
						Panel->StartNextAsyncEvaluation();
						return;
					}

					FGaeaTerrainDatasetMetadata Metadata;
					Metadata.HeightScale = HeightScale;
					const FName SourceId = bEvaluateFinal ? FinalTerrainSource : InspectionTerrainSource;
					const uint64 BaseRevision = FGaeaTerrainDatasetRegistry::Publish(SourceId, MoveTemp(BaseDataset), Metadata);
					if (BaseRevision == 0)
					{
						Panel->bAutoPreviewEvaluating = false;
						if (bEvaluateFinal)
						{
							FGaeaTerrainOutputEditorState::Get().FailAnalysis(TEXT("Publishing the evaluated terrain snapshot failed."));
						}
						SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
						Panel->StatusText = FText::FromString(TEXT("Terrain evaluated successfully, but publishing the snapshot failed."));
						Panel->StartNextAsyncEvaluation();
						return;
					}

					if (!bEvaluateFinal)
					{
						Panel->bAutoPreviewEvaluating = false;
						SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
						Panel->StatusText = FText::FromString(FString::Printf(
							TEXT("Inspected node %s -> revision %llu (%d fields)."),
							*InspectionNodeId.ToString(EGuidFormats::Short),
							static_cast<unsigned long long>(BaseRevision),
							BaseFieldCount));
						Panel->OnEvaluated.ExecuteIfBound();
						Panel->StartNextAsyncEvaluation();
						return;
					}

					FGaeaTerrainOutputEditorState::Get().PublishTerrain(BaseRevision);
					SetGraphActivity(Panel, EGaeaEditorGraphActivity::Analyzing);
					Panel->StatusText = FText::FromString(FString::Printf(
						TEXT("Terrain %08X ready in %.1f ms: %d node%s recomputed, %d cached. Deriving hydrology..."),
						RecipeHash,
						EvaluationMilliseconds,
						EvaluatedNodeCount,
						EvaluatedNodeCount == 1 ? TEXT("") : TEXT("s"),
						CachedNodeCount));
					Panel->OnEvaluated.ExecuteIfBound();

					Panel->bAutoPreviewEvaluating = false;
					Panel->StartNextAsyncEvaluation();

					Async(EAsyncExecution::ThreadPool,
						[WeakPanel,
						 Context = MoveTemp(Context),
						 Dataset = MoveTemp(Result.Dataset),
						 HeightScale,
						 RecipeHash,
						 CapturedGraphGeneration]() mutable
						{
							FString HydrologyError;
							const bool bHydrologyReady = FGaeaTerrainDerivedData::EnsureHydrology(
								Dataset,
								HeightScale,
								Context.PhysicalMetrics,
								&HydrologyError);

							AsyncTask(ENamedThreads::GameThread,
								[WeakPanel,
								 Dataset = MoveTemp(Dataset),
								 HeightScale,
								 RecipeHash,
								 CapturedGraphGeneration,
								 bHydrologyReady,
								 HydrologyError = MoveTemp(HydrologyError)]() mutable
								{
									const TSharedPtr<SGaeaTerrainGraphPanel> Panel = WeakPanel.Pin();
									if (!Panel.IsValid()) return;
									if (CapturedGraphGeneration != Panel->GraphEvaluationGeneration) return;

									if (!bHydrologyReady)
									{
										FGaeaTerrainOutputEditorState::Get().FailDerivedAnalysis(HydrologyError);
										SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
										Panel->StatusText = FText::FromString(FString::Printf(
											TEXT("Terrain is ready, but hydrology analysis failed: %s"),
											*HydrologyError));
										return;
									}

									FGaeaTerrainDatasetMetadata Metadata;
									Metadata.HeightScale = HeightScale;
									const int32 FieldCount = Dataset.NumScalarFields();
									const uint64 AnalysisRevision = FGaeaTerrainDatasetRegistry::Publish(
										FinalTerrainSource,
										MoveTemp(Dataset),
										Metadata);
									if (AnalysisRevision == 0)
									{
										FGaeaTerrainOutputEditorState::Get().FailDerivedAnalysis(TEXT("Publishing hydrology analysis failed."));
										SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
										Panel->StatusText = FText::FromString(TEXT("Terrain is ready, but publishing hydrology analysis failed."));
										return;
									}

									FGaeaTerrainOutputEditorState::Get().CompleteAnalysis(AnalysisRevision);
									SetGraphActivity(Panel, EGaeaEditorGraphActivity::Idle);
									Panel->StatusText = FText::FromString(FString::Printf(
										TEXT("Analysis %08X -> revision %llu (%d fields, hydrology ready)."),
										RecipeHash,
										static_cast<unsigned long long>(AnalysisRevision),
										FieldCount));
									Panel->OnEvaluated.ExecuteIfBound();
								});
						});
				});
		});
}
