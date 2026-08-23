#include "SGaeaTerrainMeshPreview.h"

#include "Components/BaseDynamicMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "EditorViewportClient.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainMeshMaterializer.h"
#include "GaeaTerrainOutputEditorState.h"
#include "Materials/Material.h"

namespace
{
	constexpr double PreviewCentimetersPerKilometer = 100000.0;
	constexpr double PreviewCentimetersPerMeter = 100.0;
	constexpr int32 PreviewMeshResolution = 257;
}

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
		PreviewMeshComponent->SetColorOverrideMode(EDynamicMeshComponentColorOverrideMode::VertexColors);
		PreviewMeshComponent->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::LinearToSRGB);
		PreviewScene.AddComponent(PreviewMeshComponent, FTransform::Identity);
	}

	LastOutputSettingsRevision = FGaeaTerrainOutputEditorState::Get().GetRevision();
	StatusText = FText::FromString(TEXT("Drop/connect a terrain-capable graph output to preview it here."));
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

void SGaeaTerrainMeshPreview::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const uint64 CurrentRevision = FGaeaTerrainOutputEditorState::Get().GetRevision();
	if (CurrentRevision != LastOutputSettingsRevision && LastSnapshot.IsValid())
	{
		SetTerrain(LastSnapshot);
	}
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

void SGaeaTerrainMeshPreview::ClearTerrain()
{
	LastSnapshot = FGaeaTerrainDatasetSnapshot();
	LastOutputSettingsRevision = FGaeaTerrainOutputEditorState::Get().GetRevision();
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

	LastSnapshot = Snapshot;
	LastOutputSettingsRevision = FGaeaTerrainOutputEditorState::Get().GetRevision();

	FGaeaTerrainMeshBuildOptions Options;
	Options.HeightScale = Snapshot.Metadata.HeightScale;
	Options.HorizontalScale = 1.0;
	Options.VerticalScale = 1.0;

	// This is an editor viewport thumbnail, not the generated terrain. Never build a
	// native-resolution DynamicMesh here: a 4K source would otherwise create ~16M
	// preview vertices on the game thread. The output panel still uses the requested
	// full resolution when Generate Terrain is pressed.
	const FGaeaScalarField* Height = Snapshot.Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (Height && Height->IsValid())
	{
		Options.TargetResolution = FIntPoint(
			FMath::Min(PreviewMeshResolution, Height->Domain.Dimensions.X),
			FMath::Min(PreviewMeshResolution, Height->Domain.Dimensions.Y));

		const FVector2d SourceSize = Height->Domain.WorldSize();
		const double SourceWidth = FMath::Abs(SourceSize.X);
		const double SourceDepth = FMath::Abs(SourceSize.Y);
		if (SourceWidth > UE_DOUBLE_SMALL_NUMBER && SourceDepth > UE_DOUBLE_SMALL_NUMBER)
		{
			const FGaeaTerrainGraphOutputSettings& Output = FGaeaTerrainOutputEditorState::Get().GetSettings();
			Options.HorizontalScaleXY = FVector2d(
				Output.WorldWidthKilometers * PreviewCentimetersPerKilometer / SourceWidth,
				Output.WorldDepthKilometers * PreviewCentimetersPerKilometer / SourceDepth);
			Options.VerticalScale =
				Output.ElevationScaleMeters * PreviewCentimetersPerMeter /
				FMath::Max(static_cast<double>(Snapshot.Metadata.HeightScale), UE_DOUBLE_SMALL_NUMBER);
		}
	}
	else
	{
		Options.TargetResolution = FIntPoint(PreviewMeshResolution, PreviewMeshResolution);
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	FString Error;
	if (!FGaeaTerrainMeshMaterializer::BuildDynamicMesh(Snapshot.Dataset, Options, Mesh, &Error))
	{
		LastSnapshot = Snapshot;
		if (PreviewMeshComponent)
		{
			UE::Geometry::FDynamicMesh3 EmptyMesh;
			PreviewMeshComponent->SetMesh(MoveTemp(EmptyMesh));
		}
		StatusText = FText::FromString(Error.IsEmpty() ? TEXT("This output cannot be rendered as a terrain mesh.") : Error);
		return;
	}

	const int32 VertexCount = Mesh.VertexCount();
	const int32 TriangleCount = Mesh.TriangleCount();
	const bool bHasBaseColor = Mesh.HasVertexColors();
	PreviewMeshComponent->SetMesh(MoveTemp(Mesh));
	PreviewMeshComponent->SetColorOverrideMode(
		bHasBaseColor ? EDynamicMeshComponentColorOverrideMode::VertexColors : EDynamicMeshComponentColorOverrideMode::None);
	StatusText = FText::FromString(FString::Printf(
		bHasBaseColor
			? TEXT("Preview %d vertices   %d triangles   SatMap color   sea level 0 m")
			: TEXT("Preview %d vertices   %d triangles   sea level 0 m"),
		VertexCount,
		TriangleCount));
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
