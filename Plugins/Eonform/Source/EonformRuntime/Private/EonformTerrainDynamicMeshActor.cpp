#include "EonformTerrainDynamicMeshActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "EonformTerrainMeshMaterializer.h"

AEonformTerrainDynamicMeshActor::AEonformTerrainDynamicMeshActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("TerrainMesh"));
	SetRootComponent(TerrainMesh);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainMesh->SetGenerateOverlapEvents(false);
	TerrainMesh->SetCastShadow(true);
}

bool AEonformTerrainDynamicMeshActor::ApplyTerrainDataset(
	const FEonformTerrainDataset& Dataset,
	float HeightScale,
	FString* OutError)
{
	FEonformTerrainMeshBuildOptions Options;
	Options.HeightScale = HeightScale;
	return ApplyTerrainDataset(Dataset, Options, OutError);
}

bool AEonformTerrainDynamicMeshActor::ApplyTerrainDataset(
	const FEonformTerrainDataset& Dataset,
	const FEonformTerrainMeshBuildOptions& Options,
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
	if (!FEonformTerrainMeshMaterializer::BuildDynamicMesh(Dataset, Options, Mesh, OutError))
	{
		return false;
	}

	TerrainMesh->SetMesh(MoveTemp(Mesh));
	return true;
}
