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

		// Opening/switching assets is editor navigation, not a terrain edit. Reset the
		// semantic-preview baseline so the newly loaded graph is accepted as-is on the
		// next poll instead of being compared against the previous asset and evaluated.
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
