#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "PreviewScene.h"
#include "SEditorViewport.h"

class FEditorViewportClient;
class UDynamicMeshComponent;

/** Embedded, editor-only 3D preview of the latest evaluated EONFORM terrain. */
class SGaeaTerrainMeshPreview : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainMeshPreview) {}
	SLATE_END_ARGS()

	SGaeaTerrainMeshPreview();
	virtual ~SGaeaTerrainMeshPreview() override;

	void Construct(const FArguments& InArgs);
	void SetTerrain(const FGaeaTerrainDatasetSnapshot& Snapshot);
	void ClearTerrain();
	FText GetStatusText() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	void FrameTerrain();

	FPreviewScene PreviewScene;
	TSharedPtr<FEditorViewportClient> PreviewViewportClient;
	UDynamicMeshComponent* PreviewMeshComponent = nullptr;
	FText StatusText;
};
