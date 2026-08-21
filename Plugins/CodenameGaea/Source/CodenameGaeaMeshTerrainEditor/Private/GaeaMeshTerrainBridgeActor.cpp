#include "GaeaMeshTerrainBridgeActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "GaeaTerrainDatasetRegistry.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainGraphAsset.h"
#include "GaeaTerrainMeshMaterializer.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"

using UE::Geometry::FDynamicMesh3;
using UE::MeshPartition::AMeshPartition;
using UE::MeshPartition::UMeshPartitionDefinition;
using UE::MeshPartition::UMeshProviderModifier;

AGaeaMeshTerrainBridgeActor::AGaeaMeshTerrainBridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	EditorMarker = CreateDefaultSubobject<UArrowComponent>(TEXT("EditorMarker"));
	EditorMarker->SetupAttachment(SceneRoot);
	EditorMarker->ArrowSize = 2.0f;
	EditorMarker->SetHiddenInGame(true);
	EditorMarker->SetIsVisualizationComponent(true);

	UMeshProviderModifier* MeshProvider = CreateDefaultSubobject<UMeshProviderModifier>(TEXT("EONFORMMeshProvider"));
	MeshProvider->SetupAttachment(SceneRoot);
	MeshProviderComponent = MeshProvider;

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

AActor* AGaeaMeshTerrainBridgeActor::ResolveOrCreateMeshPartitionActor()
{
	if (IsValid(TargetMeshPartition.Get()))
	{
		if (Cast<AMeshPartition>(TargetMeshPartition.Get()))
		{
			return TargetMeshPartition.Get();
		}
		SetStatus(TEXT("Target Mesh Partition is assigned but is not an AMeshPartition actor."), true);
		return nullptr;
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
		SetStatus(TEXT("UE 5.8 failed to spawn an AMeshPartition. Ensure Mesh Partition is enabled and this is an editor world."), true);
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

	UMeshProviderModifier* MeshProvider = Cast<UMeshProviderModifier>(MeshProviderComponent.Get());
	if (!MeshProvider)
	{
		SetStatus(TEXT("EONFORM Mesh Provider component is unavailable."), true);
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

	const int32 VertexCount = DynamicMesh.VertexCount();
	const int32 TriangleCount = DynamicMesh.TriangleCount();
	if (TriangleCount <= 0 || VertexCount <= 0)
	{
		SetStatus(TEXT("The evaluated graph produced an empty terrain mesh."), true);
		return;
	}

	AMeshPartition* Partition = Cast<AMeshPartition>(ResolveOrCreateMeshPartitionActor());
	if (!Partition)
	{
		return;
	}

	UMeshPartitionDefinition* Definition = Cast<UMeshPartitionDefinition>(MeshPartitionDefinition.Get());
	if (!Definition)
	{
		SetStatus(TEXT("Assign a UE 5.8 Mesh Partition Definition asset before building Mesh Terrain."), true);
		return;
	}

	Partition->Modify();
	MeshProvider->Modify();
	Partition->SetMeshPartitionDefinition(Definition);
	MeshProvider->BP_SetAffectedMegaMesh(Partition);
	MeshProvider->SetMesh(MoveTemp(DynamicMesh), true);

	SetStatus(FString::Printf(
		TEXT("Built Mesh Terrain: %d vertices, %d triangles, graph hash %u."),
		VertexCount,
		TriangleCount,
		Result.RecipeHash),
		false);
}

void AGaeaMeshTerrainBridgeActor::ClearMeshTerrain()
{
	UMeshProviderModifier* MeshProvider = Cast<UMeshProviderModifier>(MeshProviderComponent.Get());
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
