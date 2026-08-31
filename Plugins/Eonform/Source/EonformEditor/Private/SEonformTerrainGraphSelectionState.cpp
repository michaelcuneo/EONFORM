#include "SEonformTerrainGraphPanel.h"

#include "EonformTerrainGraphSelectionState.h"

void SEonformTerrainGraphPanel::SyncGraphSelectionState()
{
	FEonformTerrainGraphSelectionState& SelectionState = FEonformTerrainGraphSelectionState::Get();
	const uint64 SelectionRevision = SelectionState.GetRevision();
	if (SelectionRevision == LastGraphSelectionRevision)
	{
		return;
	}

	LastGraphSelectionRevision = SelectionRevision;
	UEonformTerrainGraphAsset* SelectedAsset = SelectionState.GetSelected();
	if (SelectedAsset && SelectedAsset != CurrentAsset.Get())
	{
		LoadAsset(*SelectedAsset);
	}
}
