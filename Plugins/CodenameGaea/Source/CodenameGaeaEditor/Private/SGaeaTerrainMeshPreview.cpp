#include "SGaeaTerrainMeshPreview.h"

#include "Components/DynamicMeshComponent.h"
#include "EditorViewportClient.h"
#include "GaeaTerrainMeshMaterializer.h"
#include "Materials/Material.h"
#include "Widgets/SNullWidget.h"

SGaeaTerrainMeshPreview::SGaeaTerrainMeshPreview()
	: PreviewScene(FPreviewScene::ConstructionValues())
{
}

SGaeaTerrainMeshPreview::~SGaeaTerrainMeshPreview()
{
	if (PreviewMeshComponent)
	{
		PreviewScene.RemoveComponent(PreviewMeshComponent);
		PreviewMeshComponent = nullptr;
	}
}

void SGaeaTerrainMeshPreview::Construct(const FArguments& InArgs)
{
	PreviewMeshComponent = NewObject<UDynamicMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMeshComponent->SetGenerateOverlapEvents(false);
		PreviewMeshComponent->SetCastShadow(true);
		PreviewMeshComponent->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
		PreviewScene.AddComponent(PreviewMeshComponent, FTransform::Identity);
	}

	StatusText = FText::FromString(TEXT("Drop/connect a terrain-capable graph output to preview it here."));
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SGaeaTerrainMeshPreview::MakeEditorViewportClient()
{
	PreviewViewportClient = MakeShared<FEditorViewportClient>(nullptr, &PreviewScene);
	PreviewViewportClient->SetViewportType(LVT_Perspective);
	PreviewViewportClient->SetViewLocation(FVector(-1200.0, -1200.0, 900.0));
	PreviewViewportClient->SetViewRotation(FRotator(-25.0, 45.0, 0.0));
	PreviewViewportClient->SetRealtime(true);
	PreviewViewportClient->SetViewMode(VMI_Lit);
	return PreviewViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SGaeaTerrainMeshPreview::MakeViewportToolbar()
{
	return SNullWidget::NullWidget;
}

void SGaeaTerrainMeshPreview::ClearTerrain()
{
	if (PreviewMeshComponent)
	{
		UE::Geometry::FDynamicMesh3 EmptyMesh;
		PreviewMeshComponent->SetMesh(MoveTemp(EmptyMesh));
	}
	StatusText = FText::FromString(TEXT("No mesh-previewable Height field is available."));
}

void SGaeaTerrainMeshPreview::SetTerrain(const FGaeaTerrainDatasetSnapshot& Snapshot)
{
	if (!PreviewMeshComponent || !Snapshot.IsValid())
	{
		ClearTerrain();
		return;
	}

	FGaeaTerrainMeshBuildOptions Options;
	Options.HeightScale = Snapshot.Metadata.HeightScale;
	Options.HorizontalScale = 1.0;
	Options.VerticalScale = 1.0;
	Options.TargetResolution = FIntPoint::ZeroValue;

	UE::Geometry::FDynamicMesh3 Mesh;
	FString Error;
	if (!FGaeaTerrainMeshMaterializer::BuildDynamicMesh(Snapshot.Dataset, Options, Mesh, &Error))
	{
		ClearTerrain();
		StatusText = FText::FromString(Error.IsEmpty() ? TEXT("This output cannot be rendered as a terrain mesh.") : Error);
		return;
	}

	const int32 VertexCount = Mesh.VertexCount();
	const int32 TriangleCount = Mesh.TriangleCount();
	PreviewMeshComponent->SetMesh(MoveTemp(Mesh));
	StatusText = FText::FromString(FString::Printf(TEXT("%d vertices   %d triangles"), VertexCount, TriangleCount));
	FrameTerrain();
}

void SGaeaTerrainMeshPreview::FrameTerrain()
{
	if (!PreviewMeshComponent || !PreviewViewportClient.IsValid())
	{
		return;
	}

	const FBoxSphereBounds Bounds = PreviewMeshComponent->Bounds;
	if (Bounds.SphereRadius <= UE_SMALL_NUMBER)
	{
		return;
	}

	PreviewViewportClient->FocusViewportOnBox(Bounds.GetBox(), false);
	PreviewViewportClient->Invalidate();
}

FText SGaeaTerrainMeshPreview::GetStatusText() const
{
	return StatusText;
}
