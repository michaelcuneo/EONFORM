#pragma once

#include "CoreMinimal.h"

class UEonformTerrainGraphAsset;

/** Editor-session selection shared by the workspace graph picker and graph panel. */
class FEonformTerrainGraphSelectionState
{
public:
	static FEonformTerrainGraphSelectionState& Get()
	{
		static FEonformTerrainGraphSelectionState State;
		return State;
	}

	void SetSelected(UEonformTerrainGraphAsset* Asset)
	{
		if (SelectedAsset.Get() == Asset) return;
		SelectedAsset = Asset;
		++Revision;
	}

	UEonformTerrainGraphAsset* GetSelected() const { return SelectedAsset.Get(); }
	uint64 GetRevision() const { return Revision; }

private:
	TWeakObjectPtr<UEonformTerrainGraphAsset> SelectedAsset;
	uint64 Revision = 1;
};
