#include "SEonformTerrainGraphPanel.h"

#include "EonformTerrainDatasetRegistry.h"
#include "EonformTerrainOutputEditorState.h"

void SEonformTerrainGraphPanel::SyncOutputSettingsState()
{
	FEonformTerrainOutputEditorState& OutputState = FEonformTerrainOutputEditorState::Get();
	UEonformTerrainGraphAsset* Asset = CurrentAsset.Get();

	if (Asset != LastOutputSettingsAsset.Get())
	{
		LastOutputSettingsAsset = Asset;
		if (Asset)
		{
			OutputState.Load(Asset->OutputSettings);
		}
		else
		{
			OutputState.Reset();
		}
		LastOutputSettingsRevision = OutputState.GetRevision();

		// Invalidate all work and all published snapshots from the previous asset.
		// A worker may finish, but its generation can no longer match and therefore
		// it cannot publish. Generate Terrain also cannot consume the previous graph.
		++GraphEvaluationGeneration;
		++InspectionEvaluationGeneration;
		bFinalEvaluationPending = false;
		PendingInspectionNodeId.Invalidate();
		FEonformTerrainDatasetRegistry::Remove(TEXT("EonformGraph"));
		FEonformTerrainDatasetRegistry::Remove(TEXT("EonformInspection"));
		OutputState.InvalidateAnalysis();

		// Never let cached node outputs cross graph assets. Replacing the shared
		// cache rather than mutating it also leaves any obsolete worker with a safe,
		// private cache instance until that worker naturally exits.
		IncrementalEvaluationCache = MakeShared<FEonformTerrainEvaluationCache, ESPMode::ThreadSafe>();

		bAutoPreviewInitialized = false;
		AutoPreviewPollAccumulator = 0.0f;
		LastAutoPreviewHash = 0;
		LastPreviewNodeId.Invalidate();
		return;
	}

	if (!Asset)
	{
		return;
	}

	const uint64 StateRevision = OutputState.GetRevision();
	if (StateRevision == LastOutputSettingsRevision)
	{
		return;
	}

	Asset->Modify();
	Asset->OutputSettings = OutputState.GetSettings();
	Asset->MarkPackageDirty();
	LastOutputSettingsRevision = StateRevision;
}
