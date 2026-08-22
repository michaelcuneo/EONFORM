#include "SGaeaTerrainGraphPanel.h"

#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainOutputEditorState.h"

void SGaeaTerrainGraphPanel::SyncOutputSettingsState()
{
	FGaeaTerrainOutputEditorState& OutputState = FGaeaTerrainOutputEditorState::Get();
	UGaeaTerrainGraphAsset* Asset = CurrentAsset.Get();

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

		// Invalidate all work captured from the previous asset. A worker may finish,
		// but its generation can no longer match and therefore it cannot publish.
		++GraphEvaluationGeneration;
		++InspectionEvaluationGeneration;
		bFinalEvaluationPending = false;
		PendingInspectionNodeId.Invalidate();
		FGaeaTerrainDatasetRegistry::Remove(TEXT("CodenameGaeaInspection"));

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
