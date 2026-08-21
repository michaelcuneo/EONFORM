#include "GaeaTerrainDynamicMeshActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GaeaTerrainMeshMaterializer.h"

AGaeaTerrainDynamicMeshActor::AGaeaTerrainDynamicMeshActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("TerrainMesh"));
	SetRootComponent(TerrainMesh);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainMesh->SetGenerateOverlapEvents(false);
	TerrainMesh->SetCastShadow(true);
}

bool AGaeaTerrainDynamicMeshActor::ApplyTerrainDataset(
	const FGaeaTerrainDataset& Dataset,
	float HeightScale,
	FString* OutError)
{
	FGaeaTerrainMeshBuildOptions Options;
	Options.HeightScale = HeightScale;
	return ApplyTerrainDataset(Dataset, Options, OutError);
}

bool AGaeaTerrainDynamicMeshActor::ApplyTerrainDataset(
	const FGaeaTerrainDataset& Dataset,
	const FGaeaTerrainMeshBuildOptions& Options,
	FString* OutError)
{
	if (!TerrainMesh)
	{
		if (OutError)
		{
			*OutError = TEXT("Terrain Dynamic Mesh component is unavailable.");
		}
		return false;
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	if (!FGaeaTerrainMeshMaterializer::BuildDynamicMesh(Dataset, Options, Mesh, OutError))
	{
		return false;
	}

	TerrainMesh->SetMesh(MoveTemp(Mesh));
	return true;
}
