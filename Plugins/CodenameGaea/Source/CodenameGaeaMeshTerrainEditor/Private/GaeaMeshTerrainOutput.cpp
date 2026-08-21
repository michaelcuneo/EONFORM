#include "GaeaMeshTerrainOutput.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GaeaTerrainMeshMaterializer.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"

using UE::Geometry::FDynamicMesh3;
using UE::MeshPartition::AMeshPartition;
using UE::MeshPartition::UMeshPartitionDefinition;
using UE::MeshPartition::UMeshProviderModifier;

FGaeaMeshTerrainBuildResult FGaeaMeshTerrainOutput::Build(
	UWorld* World,
	const FGaeaTerrainDataset& Dataset,
	float HeightScale,
	UObject* PreferredMeshPartitionDefinition,
	AActor* PreferredMeshPartition)
{
	FGaeaMeshTerrainBuildResult Result;

	if (!World)
	{
		Result.Message = TEXT("The editor world is not available.");
		return Result;
	}

	FDynamicMesh3 DynamicMesh;
	FString MaterializeError;
	if (!FGaeaTerrainMeshMaterializer::BuildDynamicMesh(Dataset, HeightScale, DynamicMesh, &MaterializeError))
	{
		Result.Message = FString::Printf(TEXT("Mesh materialization failed: %s"), *MaterializeError);
		return Result;
	}

	Result.VertexCount = DynamicMesh.VertexCount();
	Result.TriangleCount = DynamicMesh.TriangleCount();
	if (Result.VertexCount <= 0 || Result.TriangleCount <= 0)
	{
		Result.Message = TEXT("The evaluated graph produced an empty terrain mesh.");
		return Result;
	}

	AMeshPartition* Partition = Cast<AMeshPartition>(PreferredMeshPartition);
	if (!Partition)
	{
		for (TActorIterator<AMeshPartition> It(World); It; ++It)
		{
#if WITH_EDITOR
			if (It->GetActorLabel() == TEXT("EONFORM Mesh Terrain"))
			{
				Partition = *It;
				break;
			}
#endif
		}
	}

	if (!Partition)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(World, AMeshPartition::StaticClass(), TEXT("EONFORM_MeshTerrain"));
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transactional;

		Partition = World->SpawnActor<AMeshPartition>(
			AMeshPartition::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParameters);
		if (!Partition)
		{
			Result.Message = TEXT("UE 5.8 failed to create the EONFORM Mesh Partition actor.");
			return Result;
		}
#if WITH_EDITOR
		Partition->SetActorLabel(TEXT("EONFORM Mesh Terrain"));
#endif
	}

	UMeshPartitionDefinition* Definition = Cast<UMeshPartitionDefinition>(PreferredMeshPartitionDefinition);
	if (!Definition)
	{
		Definition = const_cast<UMeshPartitionDefinition*>(UMeshPartitionDefinition::GetDefaultMegaMeshDefinition());
	}
	if (!Definition)
	{
		Result.Message = TEXT("No UE 5.8 Mesh Partition Definition is available.");
		return Result;
	}

	UMeshProviderModifier* MeshProvider = Partition->FindComponentByClass<UMeshProviderModifier>();
	if (!MeshProvider)
	{
		MeshProvider = NewObject<UMeshProviderModifier>(Partition, TEXT("EONFORMMeshProvider"), RF_Transactional);
		if (!MeshProvider)
		{
			Result.Message = TEXT("Failed to create the UE 5.8 Mesh Provider modifier.");
			return Result;
		}

		Partition->AddInstanceComponent(MeshProvider);
		if (USceneComponent* Root = Partition->GetRootComponent())
		{
			MeshProvider->SetupAttachment(Root);
		}
		MeshProvider->RegisterComponent();
	}

	Partition->Modify();
	MeshProvider->Modify();
	Partition->SetMeshPartitionDefinition(Definition);
	MeshProvider->BP_SetAffectedMegaMesh(Partition);
	MeshProvider->SetMesh(MoveTemp(DynamicMesh), true);

	Result.bSuccess = true;
	Result.TerrainActor = Partition;
	Result.Message = FString::Printf(
		TEXT("Built EONFORM Mesh Terrain: %d vertices, %d triangles."),
		Result.VertexCount,
		Result.TriangleCount);
	return Result;
}
