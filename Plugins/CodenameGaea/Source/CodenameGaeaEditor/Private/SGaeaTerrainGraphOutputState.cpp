#include "SGaeaTerrainGraphPanel.h"

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

		// Opening/switching assets is navigation, not a terrain edit. Invalidate work
		// belonging to the previous graph and clear both final/selected pending queues.
		// The next auto-preview poll establishes this graph's baseline and queues its
		// authoritative final preview in the background.
		++AutoPreviewRequestSerial;
		bAutoPreviewRestartPending = false;
		bFinalAutoPreviewPending = false;
		PendingSelectedPreviewNodeId.Invalidate();
		ActiveAutoPreviewNodeId.Invalidate();
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
