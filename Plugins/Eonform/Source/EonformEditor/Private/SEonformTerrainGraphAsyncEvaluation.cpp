#include "SEonformTerrainGraphPanel.h"

#include "Async/Async.h"
#include "EonformTerrainDatasetRegistry.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainOutputEditorState.h"
#include "EonformTerrainPhysicalMetrics.h"
#include "EonformTerrainRegionalSupport.h"
#include "Modules/ModuleManager.h"

namespace
{
	const FName FinalTerrainSource(TEXT("EonformGraph"));
	const FName InspectionTerrainSource(TEXT("EonformInspection"));
	constexpr int32 InteractivePreviewMaxResolution = 505;

	bool PrepareEvaluationContext(
		const FEonformTerrainRecipe& Recipe,
		FEonformTerrainEvaluationContext& OutContext,
		FString& OutError)
	{
		OutContext = FEonformTerrainEvaluationContext();
		OutContext.PhysicalMetrics = FEonformTerrainPhysicalContext::GetActive();
		OutContext.CacheContextRevision = FEonformTerrainPhysicalContext::GetRevision();

		const FEonformTerrainRegionalSupportReport RegionalSupport = FEonformTerrainRegionalSupport::Analyze(Recipe);
		const FEonformTerrainGraphOutputSettings& OutputSettings = FEonformTerrainOutputEditorState::Get().GetSettings();
		if (OutputSettings.OutputResolution > 0)
		{
			const int32 FinalResolution = FMath::Clamp(OutputSettings.OutputResolution, 17, 8129);
			const int32 PreviewResolution = RegionalSupport.bSupported
				? FMath::Min(FinalResolution, InteractivePreviewMaxResolution)
				: FinalResolution;

			// Only proven region-safe graphs can use a cheap preview raster. Graphs
			// containing unresolved global dependencies keep the legacy full-resolution
			// preview so the existing output fallback remains behaviorally identical.
			OutContext.TargetResolution = FIntPoint(PreviewResolution, PreviewResolution);
			OutContext.ReferenceResolution = FIntPoint(FinalResolution, FinalResolution);
			OutContext.CacheContextRevision = HashCombineFast(
				GetTypeHash(OutContext.CacheContextRevision),
				GetTypeHash(FinalResolution));
			OutContext.CacheContextRevision = HashCombineFast(
				GetTypeHash(OutContext.CacheContextRevision),
				GetTypeHash(PreviewResolution));
		}

		const bool bUsesExternalSource = Recipe.Nodes.ContainsByPredicate([](const FEonformTerrainNode& Node)
		{
			return Node.Type == EonformTerrainNodeTypes::SourceDataset;
		});
		if (bUsesExternalSource)
		{
			FEonformTerrainDatasetSnapshot SourceSnapshot;
			if (!FEonformTerrainDatasetRegistry::Get(TEXT("LegacyTerrainGenerator"), SourceSnapshot) || !SourceSnapshot.IsValid())
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

		const bool bUsesFileNode = Recipe.Nodes.ContainsByPredicate([](const FEonformTerrainNode& Node)
		{
			return Node.Type == EonformTerrainNodeTypes::File;
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

	FEonformTerrainEvaluationContext MakeGenerationContext(const FEonformTerrainEvaluationContext& PreviewContext)
	{
		FEonformTerrainEvaluationContext GenerationContext = PreviewContext;
		if (GenerationContext.ReferenceResolution.X > 1 && GenerationContext.ReferenceResolution.Y > 1)
		{
			GenerationContext.TargetResolution = GenerationContext.ReferenceResolution;
		}
		GenerationContext.Region = FEonformTerrainEvaluationRegion();
		return GenerationContext;
	}

	void SetGraphActivity(const TSharedPtr<SEonformTerrainGraphPanel>& Panel, EEonformEditorGraphActivity Activity)
	{
		if (Panel.IsValid() && Panel->EditorGraph.IsValid())
		{
			Panel->EditorGraph->SetActivity(Activity);
		}
	}
}

void SEonformTerrainGraphPanel::RequestFinalEvaluationAsync()
{
	++GraphEvaluationGeneration;
	bFinalEvaluationPending = true;
	FEonformTerrainOutputEditorState::Get().BeginAnalysis();

	if (!bAutoPreviewEvaluating)
	{
		StartNextAsyncEvaluation();
	}
}

void SEonformTerrainGraphPanel::RequestInspectionEvaluationAsync(const FGuid& NodeId)
{
	if (!NodeId.IsValid()) return;

	++InspectionEvaluationGeneration;
	PendingInspectionNodeId = NodeId;
	if (!bAutoPreviewEvaluating)
	{
		StartNextAsyncEvaluation();
	}
}

void SEonformTerrainGraphPanel::ClearInspectionPreview()
{
	++InspectionEvaluationGeneration;
	PendingInspectionNodeId.Invalidate();
	FEonformTerrainDatasetRegistry::Remove(InspectionTerrainSource);
	OnEvaluated.ExecuteIfBound();
}

void SEonformTerrainGraphPanel::StartNextAsyncEvaluation()
{
	if (bAutoPreviewEvaluating) return;

	const bool bEvaluateFinal = bFinalEvaluationPending;
	FGuid InspectionNodeId;
	if (!bEvaluateFinal)
	{
		InspectionNodeId = PendingInspectionNodeId;
		if (!InspectionNodeId.IsValid())
		{
			if (EditorGraph.IsValid()) EditorGraph->SetActivity(EEonformEditorGraphActivity::Idle);
			return;
		}
	}

	FEonformTerrainRecipe Recipe;
	FString Error;
	if (!BuildRecipeFromEditorGraph(Recipe, Error))
	{
		if (bEvaluateFinal)
		{
			bFinalEvaluationPending = false;
			FEonformTerrainOutputEditorState::Get().FailAnalysis(Error);
		}
		else
		{
			PendingInspectionNodeId.Invalidate();
		}
		if (EditorGraph.IsValid()) EditorGraph->SetActivity(EEonformEditorGraphActivity::Idle);
		StatusText = FText::FromString(FString::Printf(TEXT("Graph is invalid: %s"), *Error));
		return;
	}

	if (!bEvaluateFinal)
	{
		Recipe.OutputNode = InspectionNodeId;
		if (!Recipe.Validate(&Error))
		{
			PendingInspectionNodeId.Invalidate();
			if (EditorGraph.IsValid()) EditorGraph->SetActivity(EEonformEditorGraphActivity::Idle);
			StatusText = FText::FromString(FString::Printf(TEXT("Inspection is invalid: %s"), *Error));
			return;
		}
	}

	FEonformTerrainEvaluationContext Context;
	if (!PrepareEvaluationContext(Recipe, Context, Error))
	{
		if (bEvaluateFinal)
		{
			bFinalEvaluationPending = false;
			FEonformTerrainOutputEditorState::Get().FailAnalysis(Error);
		}
		else
		{
			PendingInspectionNodeId.Invalidate();
		}
		if (EditorGraph.IsValid()) EditorGraph->SetActivity(EEonformEditorGraphActivity::Idle);
		StatusText = FText::FromString(Error);
		return;
	}

	const FEonformTerrainEvaluationContext GenerationContext = MakeGenerationContext(Context);
	const uint64 CapturedGraphGeneration = GraphEvaluationGeneration;
	const uint64 CapturedInspectionGeneration = InspectionEvaluationGeneration;
	if (bEvaluateFinal)
	{
		bFinalEvaluationPending = false;
		StatusText = FText::FromString(TEXT("Evaluating terrain preview incrementally in background..."));
	}
	else
	{
		PendingInspectionNodeId.Invalidate();
		StatusText = FText::FromString(TEXT("Inspecting selected node in background..."));
	}

	bAutoPreviewEvaluating = true;
	if (EditorGraph.IsValid()) EditorGraph->SetActivity(EEonformEditorGraphActivity::Solving);

	TWeakPtr<SEonformTerrainGraphPanel> WeakPanel = SharedThis(this);
	const TSharedPtr<FEonformTerrainEvaluationCache, ESPMode::ThreadSafe> CapturedCache = IncrementalEvaluationCache;
	Async(EAsyncExecution::ThreadPool,
		[WeakPanel,
		 CapturedCache,
		 Recipe = MoveTemp(Recipe),
		 Context = MoveTemp(Context),
		 GenerationContext,
		 bEvaluateFinal,
		 InspectionNodeId,
		 CapturedGraphGeneration,
		 CapturedInspectionGeneration]() mutable
		{
			FEonformTerrainEvaluationResult Result = bEvaluateFinal && CapturedCache.IsValid()
				? FEonformTerrainEvaluator::EvaluateIncremental(Recipe, Context, *CapturedCache)
				: FEonformTerrainEvaluator::Evaluate(Recipe, Context);
			if (!Result.bSuccess)
			{
				const FString Failure = Result.Error;
				AsyncTask(ENamedThreads::GameThread,
					[WeakPanel, bEvaluateFinal, CapturedGraphGeneration, CapturedInspectionGeneration, Failure]()
					{
						const TSharedPtr<SEonformTerrainGraphPanel> Panel = WeakPanel.Pin();
						if (!Panel.IsValid()) return;
						Panel->bAutoPreviewEvaluating = false;
						const bool bStale = bEvaluateFinal
							? CapturedGraphGeneration != Panel->GraphEvaluationGeneration
							: CapturedInspectionGeneration != Panel->InspectionEvaluationGeneration;
						if (!bStale)
						{
							if (bEvaluateFinal) FEonformTerrainOutputEditorState::Get().FailAnalysis(Failure);
							Panel->StatusText = FText::FromString(FString::Printf(
								TEXT("%s failed: %s"),
								bEvaluateFinal ? TEXT("Terrain preview evaluation") : TEXT("Node inspection"),
								*Failure));
						}
						SetGraphActivity(Panel, EEonformEditorGraphActivity::Idle);
						Panel->StartNextAsyncEvaluation();
					});
				return;
			}

			FEonformTerrainDataset BaseDataset = MoveTemp(Result.Dataset);
			const float HeightScale = Result.HeightScale;
			const uint32 RecipeHash = Result.RecipeHash;
			const int32 BaseFieldCount = BaseDataset.NumScalarFields();
			const int32 EvaluatedNodeCount = Result.EvaluatedNodeCount;
			const int32 CachedNodeCount = Result.CachedNodeCount;
			const double EvaluationMilliseconds = Result.EvaluationMilliseconds;
			FEonformTerrainRecipe GenerationRecipe = Recipe;

			AsyncTask(ENamedThreads::GameThread,
				[WeakPanel,
				 bEvaluateFinal,
				 InspectionNodeId,
				 CapturedGraphGeneration,
				 CapturedInspectionGeneration,
				 BaseDataset = MoveTemp(BaseDataset),
				 HeightScale,
				 RecipeHash,
				 BaseFieldCount,
				 EvaluatedNodeCount,
				 CachedNodeCount,
				 EvaluationMilliseconds,
				 GenerationRecipe = MoveTemp(GenerationRecipe),
				 GenerationContext]() mutable
				{
					const TSharedPtr<SEonformTerrainGraphPanel> Panel = WeakPanel.Pin();
					if (!Panel.IsValid()) return;

					const bool bStale = bEvaluateFinal
						? CapturedGraphGeneration != Panel->GraphEvaluationGeneration
						: CapturedInspectionGeneration != Panel->InspectionEvaluationGeneration;
					if (bStale)
					{
						Panel->bAutoPreviewEvaluating = false;
						SetGraphActivity(Panel, EEonformEditorGraphActivity::Idle);
						Panel->StartNextAsyncEvaluation();
						return;
					}

					FEonformTerrainDatasetMetadata Metadata;
					Metadata.HeightScale = HeightScale;
					const FName SourceId = bEvaluateFinal ? FinalTerrainSource : InspectionTerrainSource;
					const uint64 BaseRevision = FEonformTerrainDatasetRegistry::Publish(SourceId, MoveTemp(BaseDataset), Metadata);
					if (BaseRevision == 0)
					{
						Panel->bAutoPreviewEvaluating = false;
						if (bEvaluateFinal)
						{
							FEonformTerrainOutputEditorState::Get().FailAnalysis(TEXT("Publishing the evaluated terrain snapshot failed."));
						}
						SetGraphActivity(Panel, EEonformEditorGraphActivity::Idle);
						Panel->StatusText = FText::FromString(TEXT("Terrain evaluated successfully, but publishing the snapshot failed."));
						Panel->StartNextAsyncEvaluation();
						return;
					}

					if (!bEvaluateFinal)
					{
						Panel->bAutoPreviewEvaluating = false;
						SetGraphActivity(Panel, EEonformEditorGraphActivity::Idle);
						Panel->StatusText = FText::FromString(FString::Printf(
							TEXT("Inspected node %s -> revision %llu (%d fields)."),
							*InspectionNodeId.ToString(EGuidFormats::Short),
							static_cast<unsigned long long>(BaseRevision),
							BaseFieldCount));
						Panel->OnEvaluated.ExecuteIfBound();
						Panel->StartNextAsyncEvaluation();
						return;
					}

					FEonformTerrainOutputEditorState& OutputState = FEonformTerrainOutputEditorState::Get();
					OutputState.SetGenerationPlan(GenerationRecipe, GenerationContext, CapturedGraphGeneration);
					OutputState.CompleteAnalysis(BaseRevision);
					SetGraphActivity(Panel, EEonformEditorGraphActivity::Idle);
					const bool bRegional = FEonformTerrainRegionalSupport::Analyze(GenerationRecipe).bSupported;
					FString StatusMessage;
					if (bRegional)
					{
						StatusMessage = FString::Printf(
							TEXT("Terrain %08X preview ready in %.1f ms: %d node%s recomputed, %d cached, %d fields. Final output will evaluate Mesh Terrain regions on demand."),
							RecipeHash,
							EvaluationMilliseconds,
							EvaluatedNodeCount,
							EvaluatedNodeCount == 1 ? TEXT("") : TEXT("s"),
							CachedNodeCount,
							BaseFieldCount);
					}
					else
					{
						StatusMessage = FString::Printf(
							TEXT("Terrain %08X ready in %.1f ms: %d node%s recomputed, %d cached, %d fields. This graph still contains global/nonregional nodes, so final output uses the exact legacy full-world fallback."),
							RecipeHash,
							EvaluationMilliseconds,
							EvaluatedNodeCount,
							EvaluatedNodeCount == 1 ? TEXT("") : TEXT("s"),
							CachedNodeCount,
							BaseFieldCount);
					}
					Panel->StatusText = FText::FromString(MoveTemp(StatusMessage));
					Panel->OnEvaluated.ExecuteIfBound();

					Panel->bAutoPreviewEvaluating = false;
					Panel->StartNextAsyncEvaluation();
				});
		});
}
