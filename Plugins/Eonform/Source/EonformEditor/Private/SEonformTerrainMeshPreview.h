#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainDatasetRegistry.h"
#include "PreviewScene.h"
#include "SEditorViewport.h"

class FEditorViewportClient;
class UDynamicMeshComponent;

/** Embedded, editor-only 3D preview of the latest evaluated EONFORM terrain. */
class SEonformTerrainMeshPreview : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SEonformTerrainMeshPreview) {}
	SLATE_END_ARGS()

	SEonformTerrainMeshPreview();
	virtual ~SEonformTerrainMeshPreview() override;

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	void SetTerrain(const FEonformTerrainDatasetSnapshot& Snapshot);
	void ClearTerrain();
	FText GetStatusText() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	void FrameTerrain();

	FPreviewScene PreviewScene;
	TSharedPtr<FEditorViewportClient> PreviewViewportClient;
	UDynamicMeshComponent* PreviewMeshComponent = nullptr;
	FEonformTerrainDatasetSnapshot LastSnapshot;
	uint64 LastOutputSettingsRevision = 0;
	FText StatusText;
};
