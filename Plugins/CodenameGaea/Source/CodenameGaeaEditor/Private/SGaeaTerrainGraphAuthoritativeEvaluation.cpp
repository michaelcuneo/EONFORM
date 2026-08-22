#include "SGaeaTerrainGraphPanel.h"

#include "GaeaTerrainDatasetRegistry.h"

TWeakPtr<SGaeaTerrainGraphPanel> SGaeaTerrainGraphPanel::ActivePanel;

bool SGaeaTerrainGraphPanel::EvaluateActiveGraphAndPublish(FString& OutError)
{
	const TSharedPtr<SGaeaTerrainGraphPanel> Panel = ActivePanel.Pin();
	if (!Panel.IsValid())
	{
		OutError = TEXT("No active EONFORM graph panel is available.");
		return false;
	}

	// Generate Terrain is a correctness barrier. Any preview worker that was started
	// from an older graph state must never be allowed to publish after this evaluation.
	++Panel->AutoPreviewRequestSerial;
	Panel->bFinalAutoPreviewPending = false;
	Panel->PendingSelectedPreviewNodeId.Invalidate();
	Panel->bAutoPreviewRestartPending = false;

	// Do not allow an old registry snapshot to masquerade as a successful evaluation.
	FGaeaTerrainDatasetRegistry::Remove(TEXT("CodenameGaeaGraph"));

	// This is intentionally synchronous because the caller is Generate Terrain. The
	// user explicitly asked to commit the current graph to UE Mesh Terrain, so we must
	// finish the exact current recipe before generation can continue.
	Panel->EvaluateGraph();

	FGaeaTerrainDatasetSnapshot Snapshot;
	if (!FGaeaTerrainDatasetRegistry::Get(TEXT("CodenameGaeaGraph"), Snapshot) || !Snapshot.IsValid())
	{
		OutError = Panel->GetStatusText().ToString();
		if (OutError.IsEmpty())
		{
			OutError = TEXT("EONFORM graph evaluation did not publish a valid Terrain Output.");
		}
		return false;
	}

	OutError.Reset();
	return true;
}
