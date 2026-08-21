#include "GaeaMeshTerrainBridgeActor.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainGraphAsset.h"
#include "GaeaTerrainMeshMaterializer.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"
#include "Engine/World.h"

using UE::Geometry::FDynamicMesh3;

AGaeaMeshTerrainBridgeActor::AGaeaMeshTerrainBridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;

	MeshProvider = CreateDefaultSubobject<UMeshProviderModifier>(TEXT("EONFORMMeshProvider"));
	SetRootComponent(MeshProvider);

	SetActorLabel(TEXT("EONFORM Mesh Terrain Bridge"));
}

void AGaeaMeshTerrainBridgeActor::SetStatus(const FString& Message, bool bError)
{
	LastBuildStatus = Message;
	if (bError)
	{
		UE_LOG(LogTemp, Error, TEXT("EONFORM Mesh Terrain: %s"), *Message);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("EONFORM Mesh Terrain: %s"), *Message);
	}
}

AMeshPartition* AGaeaMeshTerrainBridgeActor::ResolveOrCreateMeshPartition()
{
	if (IsValid(TargetMeshPartition))
	{
		return TargetMeshPartition;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		SetStatus(TEXT("Cannot create Mesh Terrain because the bridge has no valid world."), true);
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, AMeshPartition::StaticClass(), TEXT("EONFORM_MeshTerrain"));
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transactional;

	AMeshPartition* Partition = World->SpawnActor<AMeshPartition>(
		AMeshPartition::StaticClass(),
		GetActorLocation(),
		GetActorRotation(),
		SpawnParameters);

	if (!Partition)
	{
		SetStatus(TEXT("UE 5.8 failed to spawn an AMeshPartition. Ensure the Mesh Partition plugin is enabled and this is an editor world."), true);
		return nullptr;
	}

	Partition->SetActorScale3D(GetActorScale3D());
	Partition->SetActorLabel(TEXT("EONFORM Mesh Terrain"));
	TargetMeshPartition = Partition;
	return Partition;
}

void AGaeaMeshTerrainBridgeActor::BuildMeshTerrain()
{
	Modify();

	if (!GraphAsset)
	{
		SetStatus(TEXT("Assign an EONFORM Terrain Graph before building Mesh Terrain."), true);
		return;
	}

	FGaeaTerrainEvaluationContext Context;
	Context.HeightScale = FMath::Max(DefaultHeightScale, UE_SMALL_NUMBER);

	if (!SourceDatasetName.IsNone())
	{
		FGaeaTerrainDatasetSnapshot Snapshot;
		if (!FGaeaTerrainDatasetRegistry::Get(SourceDatasetName, Snapshot) || !Snapshot.IsValid())
		{
			SetStatus(FString::Printf(TEXT("Source dataset '%s' was not found in the runtime terrain registry."), *SourceDatasetName.ToString()), true);
			return;
		}
		Context.SourceDataset = Snapshot.Dataset;
		Context.HeightScale = Snapshot.Metadata.HeightScale;
	}

	const FGaeaTerrainEvaluationResult Result = FGaeaTerrainEvaluator::Evaluate(GraphAsset->Recipe, Context);
	if (!Result.bSuccess)
	{
		SetStatus(FString::Printf(TEXT("Graph evaluation failed: %s"), *Result.Error), true);
		return;
	}

	FDynamicMesh3 DynamicMesh;
	FString MaterializeError;
	if (!FGaeaTerrainMeshMaterializer::BuildDynamicMesh(Result.Dataset, Result.HeightScale, DynamicMesh, &MaterializeError))
	{
		SetStatus(FString::Printf(TEXT("Mesh materialization failed: %s"), *MaterializeError), true);
		return;
	}

	if (DynamicMesh.TriangleCount() <= 0 || DynamicMesh.VertexCount() <= 0)
	{
		SetStatus(TEXT("The evaluated graph produced an empty terrain mesh."), true);
		return;
	}

	AMeshPartition* Partition = ResolveOrCreateMeshPartition();
	if (!Partition)
	{
		return;
	}

	Partition->Modify();
	MeshProvider->Modify();

	UMeshPartitionDefinition* Definition = MeshPartitionDefinition;
	if (!Definition)
	{
		Definition = const_cast<UMeshPartitionDefinition*>(UMeshPartitionDefinition::GetDefaultMegaMeshDefinition());
	}
	if (Definition && Partition->GetMeshPartitionDefinition() != Definition)
	{
		Partition->SetMeshPartitionDefinition(Definition);
	}

	// UMeshProviderModifier is Epic's intended base layer for supplying an
	// FDynamicMesh3 directly to a Mesh Partition. No heightmap/static-mesh
	// conversion is required, which keeps EONFORM ready for true 3D topology.
	MeshProvider->BP_SetAffectedMegaMesh(Partition);
	MeshProvider->SetMesh(MoveTemp(DynamicMesh), true);

	SetStatus(FString::Printf(
		TEXT("Built Mesh Terrain: %d vertices, %d triangles, graph hash %u."),
		MeshProvider->GetMesh() ? MeshProvider->GetMesh()->VertexCount() : 0,
		MeshProvider->GetMesh() ? MeshProvider->GetMesh()->TriangleCount() : 0,
		Result.RecipeHash),
		false);
}

void AGaeaMeshTerrainBridgeActor::ClearMeshTerrain()
{
	if (!MeshProvider)
	{
		return;
	}

	Modify();
	MeshProvider->Modify();
	FDynamicMesh3 EmptyMesh;
	MeshProvider->SetMesh(MoveTemp(EmptyMesh), true);
	SetStatus(TEXT("Cleared the EONFORM Mesh Terrain base mesh."), false);
}
