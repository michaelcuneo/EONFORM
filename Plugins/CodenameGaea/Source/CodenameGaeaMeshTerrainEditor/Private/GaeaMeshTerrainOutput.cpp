#include "GaeaMeshTerrainOutput.h"

#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GaeaMeshTerrainBridgeActor.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"

using UE::Geometry::FDynamicMesh3;
using UE::MeshPartition::AMeshPartition;
using UE::MeshPartition::UMeshPartitionDefinition;
using UE::MeshPartition::UMeshProviderModifier;

namespace
{
	const FName EonformBaseRegionTag(TEXT("EONFORM.MeshTerrain.BaseRegion"));
	const FName EonformMeshTerrainFolder(TEXT("EONFORM/Mesh Terrain"));
	const FName EonformBaseRegionsFolder(TEXT("EONFORM/Mesh Terrain/Base Regions"));

	void ClearProviderMesh(UMeshProviderModifier* Provider)
	{
		if (!Provider)
		{
			return;
		}

		Provider->Modify();
		FDynamicMesh3 EmptyMesh;
		Provider->SetMesh(MoveTemp(EmptyMesh), true);
	}

	USceneComponent* EnsureRegionRoot(AActor* RegionActor)
	{
		if (!RegionActor)
		{
			return nullptr;
		}

		if (USceneComponent* ExistingRoot = RegionActor->GetRootComponent())
		{
			return ExistingRoot;
		}

		USceneComponent* RegionRoot = NewObject<USceneComponent>(RegionActor, NAME_None, RF_Transactional);
		if (!RegionRoot)
		{
			return nullptr;
		}

		RegionActor->AddInstanceComponent(RegionRoot);
		RegionActor->SetRootComponent(RegionRoot);
		RegionRoot->RegisterComponent();
		return RegionRoot;
	}

	UMeshProviderModifier* EnsureRegionProvider(AActor* RegionActor, USceneComponent* RegionRoot)
	{
		if (!RegionActor || !RegionRoot)
		{
			return nullptr;
		}

		if (UMeshProviderModifier* ExistingProvider = RegionActor->FindComponentByClass<UMeshProviderModifier>())
		{
			return ExistingProvider;
		}

		UMeshProviderModifier* MeshProvider = NewObject<UMeshProviderModifier>(
			RegionActor,
			NAME_None,
			RF_Transactional);
		if (!MeshProvider)
		{
			return nullptr;
		}

		RegionActor->AddInstanceComponent(MeshProvider);
		MeshProvider->SetupAttachment(RegionRoot);
		MeshProvider->RegisterComponent();
		return MeshProvider;
	}
}

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
	const int32 RequiredRegionCount = Sections.X * Sections.Y;

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
#if WITH_EDITOR
	// Keep the generated Mesh Partition and its implementation regions together in the
	// World Outliner. This also migrates an existing root-level EONFORM Mesh Terrain actor.
	Partition->SetFolderPath(EonformMeshTerrainFolder);
#endif

	// Invalidate any obsolete bridge provider still targeting this partition.
	for (TActorIterator<AGaeaMeshTerrainBridgeActor> It(World); It; ++It)
	{
		AGaeaMeshTerrainBridgeActor* Bridge = *It;
		if (!Bridge || Bridge->TargetMeshPartition.Get() != Partition)
		{
			continue;
		}

		if (UMeshProviderModifier* BridgeProvider = Cast<UMeshProviderModifier>(Bridge->MeshProviderComponent.Get()))
		{
			ClearProviderMesh(BridgeProvider);
		}
	}

	// Migration cleanup for the early implementation that placed providers directly
	// inside the Mesh Partition actor. These should never be recreated there.
	TInlineComponentArray<UMeshProviderModifier*> LegacyProviders;
	Partition->GetComponents(LegacyProviders);
	for (UMeshProviderModifier* Existing : LegacyProviders)
	{
		if (Existing && Existing->GetName().StartsWith(TEXT("EONFORMMeshProvider_")))
		{
			ClearProviderMesh(Existing);
			Existing->DestroyComponent();
		}
	}

	// Reuse EONFORM base-region actors instead of destroying/recreating them every build.
	// This is important in World Partition levels where actor churn also creates external
	// actor/package churn and can leave recently-destroyed UObject names reserved.
	TArray<AActor*> ExistingBaseRegions;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->Tags.Contains(EonformBaseRegionTag))
		{
			ExistingBaseRegions.Add(Actor);
		}
	}
#if WITH_EDITOR
	ExistingBaseRegions.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetActorLabel() < B.GetActorLabel();
	});
#endif

	const int32 IntervalsX = Resolution.X - 1;
	const int32 IntervalsY = Resolution.Y - 1;
	int32 RegionIndex = 0;

	for (int32 SectionY = 0; SectionY < Sections.Y; ++SectionY)
	{
		const int32 StartY = (SectionY * IntervalsY) / Sections.Y;
		const int32 EndY = ((SectionY + 1) * IntervalsY) / Sections.Y;

		for (int32 SectionX = 0; SectionX < Sections.X; ++SectionX, ++RegionIndex)
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
					TEXT("Mesh Terrain base region %d,%d failed: %s"),
					SectionX,
					SectionY,
					*MaterializeError);
				return Result;
			}

			const int32 RegionVertexCount = SectionMesh.VertexCount();
			const int32 RegionTriangleCount = SectionMesh.TriangleCount();

			AActor* RegionActor = ExistingBaseRegions.IsValidIndex(RegionIndex)
				? ExistingBaseRegions[RegionIndex]
				: nullptr;

			if (!RegionActor)
			{
				FActorSpawnParameters RegionSpawnParameters;
				// Deliberately do not request an object name. Unreal will allocate a safe unique
				// internal name even if an actor with the old name is pending destruction.
				RegionSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				RegionSpawnParameters.ObjectFlags |= RF_Transactional;

				RegionActor = World->SpawnActor<AActor>(
					AActor::StaticClass(),
					FVector::ZeroVector,
					FRotator::ZeroRotator,
					RegionSpawnParameters);
				if (!RegionActor)
				{
					Result.Message = FString::Printf(
						TEXT("Failed to create EONFORM Mesh Terrain base region actor %d,%d."),
						SectionX,
						SectionY);
					return Result;
				}
			}

			RegionActor->Modify();
			RegionActor->Tags.AddUnique(EonformBaseRegionTag);
#if WITH_EDITOR
			RegionActor->SetActorLabel(FString::Printf(TEXT("EONFORM Base Region %d,%d"), SectionX, SectionY));
			RegionActor->SetFolderPath(EonformBaseRegionsFolder);
#endif

			USceneComponent* RegionRoot = EnsureRegionRoot(RegionActor);
			if (!RegionRoot)
			{
				Result.Message = TEXT("Failed to create or resolve an EONFORM base region scene root.");
				return Result;
			}

			UMeshProviderModifier* MeshProvider = EnsureRegionProvider(RegionActor, RegionRoot);
			if (!MeshProvider)
			{
				Result.Message = TEXT("Failed to create or resolve the UE 5.8 Mesh Provider modifier for an EONFORM base region.");
				return Result;
			}

			MeshProvider->Modify();
			MeshProvider->BP_SetAffectedMegaMesh(Partition);
			MeshProvider->SetMesh(MoveTemp(SectionMesh), true);

			Result.VertexCount += RegionVertexCount;
			Result.TriangleCount += RegionTriangleCount;
			++Result.SectionCount;
		}
	}

	// If the user reduces the section count, only remove regions that are no longer needed.
	for (int32 ExtraIndex = RequiredRegionCount; ExtraIndex < ExistingBaseRegions.Num(); ++ExtraIndex)
	{
		AActor* ExtraActor = ExistingBaseRegions[ExtraIndex];
		if (!ExtraActor)
		{
			continue;
		}

		TInlineComponentArray<UMeshProviderModifier*> ExtraProviders;
		ExtraActor->GetComponents(ExtraProviders);
		for (UMeshProviderModifier* Provider : ExtraProviders)
		{
			ClearProviderMesh(Provider);
		}

		ExtraActor->Modify();
		ExtraActor->Destroy();
	}

	Result.bSuccess = true;
	Result.TerrainActor = Partition;
	Result.Message = FString::Printf(
		TEXT("Built EONFORM Mesh Terrain: %d base regions, %d vertices, %d triangles at %dx%d samples (XY x%.3f, Z x%.3f)."),
		Result.SectionCount,
		Result.VertexCount,
		Result.TriangleCount,
		Resolution.X,
		Resolution.Y,
		MeshOptions.HorizontalScale,
		MeshOptions.VerticalScale);
	return Result;
}
