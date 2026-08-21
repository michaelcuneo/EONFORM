#include "GaeaMeshTerrainOutput.h"

#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"

using UE::Geometry::FDynamicMesh3;
using UE::MeshPartition::AMeshPartition;
using UE::MeshPartition::UMeshPartitionDefinition;
using UE::MeshPartition::UMeshProviderModifier;

UClass* FGaeaMeshTerrainOutput::GetMeshPartitionDefinitionClass()
{
	return UMeshPartitionDefinition::StaticClass();
}

FGaeaMeshTerrainBuildResult FGaeaMeshTerrainOutput::Build(
	UWorld* World,
	const FGaeaTerrainDataset& Dataset,
	float HeightScale,
	const FGaeaMeshTerrainOutputSettings& Settings)
{
	FGaeaMeshTerrainBuildResult Result;

	if (!World)
	{
		Result.Message = TEXT("The editor world is not available.");
		return Result;
	}

	FGaeaTerrainMeshBuildOptions MeshOptions;
	MeshOptions.HeightScale = HeightScale;
	MeshOptions.HorizontalScale = FMath::Max(Settings.HorizontalScale, UE_DOUBLE_SMALL_NUMBER);
	MeshOptions.VerticalScale = FMath::Max(Settings.VerticalScale, UE_DOUBLE_SMALL_NUMBER);
	MeshOptions.TargetResolution = Settings.TargetResolution;

	const FIntPoint Resolution = FGaeaTerrainMeshMaterializer::ResolveTargetResolution(Dataset, MeshOptions);
	if (Resolution.X < 2 || Resolution.Y < 2)
	{
		Result.Message = TEXT("The resolved Mesh Terrain output resolution must be at least 2x2 samples.");
		return Result;
	}

	const int32 MaxSectionsX = FMath::Max(1, Resolution.X - 1);
	const int32 MaxSectionsY = FMath::Max(1, Resolution.Y - 1);
	const FIntPoint Sections(
		FMath::Clamp(Settings.Sections.X, 1, MaxSectionsX),
		FMath::Clamp(Settings.Sections.Y, 1, MaxSectionsY));

	AMeshPartition* Partition = Cast<AMeshPartition>(Settings.TargetMeshPartition.Get());
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

	UMeshPartitionDefinition* Definition = Cast<UMeshPartitionDefinition>(Settings.MeshPartitionDefinition.Get());
	if (!Definition)
	{
		Definition = Partition->GetMeshPartitionDefinition();
	}
	if (!Definition)
	{
		Result.Message = TEXT("Assign a UE 5.8 Mesh Partition Definition in EONFORM Terrain Output before generating terrain.");
		return Result;
	}

	Partition->Modify();
	Partition->SetMeshPartitionDefinition(Definition);

	TInlineComponentArray<UMeshProviderModifier*> ExistingProviders(Partition);
	for (UMeshProviderModifier* Existing : ExistingProviders)
	{
		if (Existing && Existing->GetName().StartsWith(TEXT("EONFORMMeshProvider_")))
		{
			Existing->Modify();
			Existing->DestroyComponent();
		}
	}

	const int32 IntervalsX = Resolution.X - 1;
	const int32 IntervalsY = Resolution.Y - 1;

	for (int32 SectionY = 0; SectionY < Sections.Y; ++SectionY)
	{
		const int32 StartY = (SectionY * IntervalsY) / Sections.Y;
		const int32 EndY = ((SectionY + 1) * IntervalsY) / Sections.Y;

		for (int32 SectionX = 0; SectionX < Sections.X; ++SectionX)
		{
			const int32 StartX = (SectionX * IntervalsX) / Sections.X;
			const int32 EndX = ((SectionX + 1) * IntervalsX) / Sections.X;

			FDynamicMesh3 SectionMesh;
			FString MaterializeError;
			if (!FGaeaTerrainMeshMaterializer::BuildDynamicMeshRegion(
				Dataset,
				MeshOptions,
				FIntPoint(StartX, StartY),
				FIntPoint(EndX, EndY),
				SectionMesh,
				&MaterializeError))
			{
				Result.Message = FString::Printf(
					TEXT("Mesh Terrain section %d,%d failed: %s"),
					SectionX,
					SectionY,
					*MaterializeError);
				return Result;
			}

			Result.VertexCount += SectionMesh.VertexCount();
			Result.TriangleCount += SectionMesh.TriangleCount();

			const FName ProviderName(*FString::Printf(TEXT("EONFORMMeshProvider_%d_%d"), SectionX, SectionY));
			UMeshProviderModifier* MeshProvider = NewObject<UMeshProviderModifier>(Partition, ProviderName, RF_Transactional);
			if (!MeshProvider)
			{
				Result.Message = TEXT("Failed to create a UE 5.8 Mesh Provider modifier for an EONFORM terrain section.");
				return Result;
			}

			Partition->AddInstanceComponent(MeshProvider);
			if (USceneComponent* Root = Partition->GetRootComponent())
			{
				MeshProvider->SetupAttachment(Root);
			}
			MeshProvider->RegisterComponent();
			MeshProvider->BP_SetAffectedMegaMesh(Partition);
			MeshProvider->SetMesh(MoveTemp(SectionMesh), true);
			++Result.SectionCount;
		}
	}

	Result.bSuccess = true;
	Result.TerrainActor = Partition;
	Result.Message = FString::Printf(
		TEXT("Built EONFORM Mesh Terrain: %d sections, %d vertices, %d triangles at %dx%d samples (XY x%.3f, Z x%.3f)."),
		Result.SectionCount,
		Result.VertexCount,
		Result.TriangleCount,
		Resolution.X,
		Resolution.Y,
		MeshOptions.HorizontalScale,
		MeshOptions.VerticalScale);
	return Result;
}
