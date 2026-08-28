#include "EonformMeshTerrainRegionalOutput.h"

#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionPlanner.h"
#include "EonformTerrainRegionalSupport.h"
#include "Materials/MaterialInterface.h"
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
	constexpr int32 RegionalEvaluationHalo = 16;

	bool HasSatMapColor(const FEonformTerrainDataset& Dataset)
	{
		const FEonformScalarField* R = Dataset.FindScalarField(EonformTerrainFieldNames::BaseColorR);
		const FEonformScalarField* G = Dataset.FindScalarField(EonformTerrainFieldNames::BaseColorG);
		const FEonformScalarField* B = Dataset.FindScalarField(EonformTerrainFieldNames::BaseColorB);
		return R && G && B && R->IsValid() && G->IsValid() && B->IsValid();
	}

	UMaterialInterface* ResolveSatMapDebugMaterial()
	{
		if (!GEngine) return nullptr;
		if (GEngine->VertexColorMaterial) return GEngine->VertexColorMaterial;
		return GEngine->VertexColorViewModeMaterial_ColorOnly;
	}

	USceneComponent* EnsureRegionRoot(AActor* RegionActor)
	{
		if (!RegionActor) return nullptr;
		if (USceneComponent* ExistingRoot = RegionActor->GetRootComponent()) return ExistingRoot;
		USceneComponent* RegionRoot = NewObject<USceneComponent>(RegionActor, NAME_None, RF_Transactional);
		if (!RegionRoot) return nullptr;
		RegionActor->AddInstanceComponent(RegionRoot);
		RegionActor->SetRootComponent(RegionRoot);
		RegionRoot->RegisterComponent();
		return RegionRoot;
	}

	UMeshProviderModifier* EnsureRegionProvider(AActor* RegionActor, USceneComponent* RegionRoot)
	{
		if (!RegionActor || !RegionRoot) return nullptr;
		if (UMeshProviderModifier* ExistingProvider = RegionActor->FindComponentByClass<UMeshProviderModifier>()) return ExistingProvider;
		UMeshProviderModifier* Provider = NewObject<UMeshProviderModifier>(RegionActor, NAME_None, RF_Transactional);
		if (!Provider) return nullptr;
		RegionActor->AddInstanceComponent(Provider);
		Provider->SetupAttachment(RegionRoot);
		Provider->RegisterComponent();
		return Provider;
	}

	AActor* FindRegionActor(UWorld* World, int32 SectionX, int32 SectionY)
	{
#if WITH_EDITOR
		const FString WantedLabel = FString::Printf(TEXT("EONFORM Base Region %d,%d"), SectionX, SectionY);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->Tags.Contains(EonformBaseRegionTag) && Actor->GetActorLabel() == WantedLabel) return Actor;
		}
#endif
		return nullptr;
	}

	AActor* EnsureRegionActor(UWorld* World, int32 SectionX, int32 SectionY, FString& OutError)
	{
		if (AActor* Existing = FindRegionActor(World, SectionX, SectionY)) return Existing;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transactional;
		AActor* RegionActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
		if (!RegionActor)
		{
			OutError = FString::Printf(TEXT("Failed to create EONFORM Mesh Terrain base region %d,%d."), SectionX, SectionY);
			return nullptr;
		}

		RegionActor->Tags.AddUnique(EonformBaseRegionTag);
#if WITH_EDITOR
		RegionActor->SetActorLabel(FString::Printf(TEXT("EONFORM Base Region %d,%d"), SectionX, SectionY));
		RegionActor->SetFolderPath(EonformBaseRegionsFolder);
#endif
		return RegionActor;
	}

	AMeshPartition* ResolvePartition(UWorld* World, const FEonformMeshTerrainOutputSettings& Settings, FString& OutError)
	{
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
			Partition = World->SpawnActor<AMeshPartition>(AMeshPartition::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
			if (!Partition)
			{
				OutError = TEXT("UE 5.8 failed to create the EONFORM Mesh Partition actor.");
				return nullptr;
			}
#if WITH_EDITOR
			Partition->SetActorLabel(TEXT("EONFORM Mesh Terrain"));
#endif
		}

		UMeshPartitionDefinition* Definition = Cast<UMeshPartitionDefinition>(Settings.MeshPartitionDefinition.Get());
		if (!Definition) Definition = Partition->GetMeshPartitionDefinition();
		if (!Definition)
		{
			OutError = TEXT("Assign a UE 5.8 Mesh Partition Definition in EONFORM Terrain Output before generating terrain.");
			return nullptr;
		}

		Partition->Modify();
		Partition->SetMeshPartitionDefinition(Definition);
#if WITH_EDITOR
		Partition->SetFolderPath(EonformMeshTerrainFolder);
#endif
		return Partition;
	}

	void RemoveUnusedRegionActors(UWorld* World, const TSet<FIntPoint>& UsedRegions)
	{
#if WITH_EDITOR
		TArray<AActor*> ToRemove;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !Actor->Tags.Contains(EonformBaseRegionTag)) continue;

			int32 X = INDEX_NONE;
			int32 Y = INDEX_NONE;
			const FString Label = Actor->GetActorLabel();
			if (Label.StartsWith(TEXT("EONFORM Base Region ")))
			{
				FString Coordinates = Label.RightChop(19);
				FString SX;
				FString SY;
				if (Coordinates.Split(TEXT(","), &SX, &SY))
				{
					X = FCString::Atoi(*SX);
					Y = FCString::Atoi(*SY);
				}
			}

			if (X == INDEX_NONE || Y == INDEX_NONE || !UsedRegions.Contains(FIntPoint(X, Y))) ToRemove.Add(Actor);
		}

		for (AActor* Actor : ToRemove)
		{
			if (Actor) Actor->Destroy();
		}
#endif
	}
}

FEonformMeshTerrainBuildResult FEonformMeshTerrainRegionalOutput::Build(
	UWorld* World,
	const FEonformTerrainRecipe& Recipe,
	const FEonformTerrainEvaluationContext& BaseContext,
	const FEonformMeshTerrainOutputSettings& Settings)
{
	FEonformMeshTerrainBuildResult BuildResult;
	if (!World)
	{
		BuildResult.Message = TEXT("The editor world is not available.");
		return BuildResult;
	}

	const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Recipe);
	if (!Support.bSupported)
	{
		BuildResult.Message = FString::Printf(TEXT("Regional generation is not yet safe for this graph: %s"), *Support.Describe());
		return BuildResult;
	}

	FIntPoint FullResolution = Settings.TargetResolution;
	if (FullResolution.X < 2 || FullResolution.Y < 2) FullResolution = BaseContext.ReferenceResolution;
	if (FullResolution.X < 2 || FullResolution.Y < 2) FullResolution = BaseContext.TargetResolution;
	if (FullResolution.X < 2 || FullResolution.Y < 2)
	{
		BuildResult.Message = TEXT("Regional generation requires an explicit final output resolution.");
		return BuildResult;
	}

	const FEonformMeshTerrainLayoutEstimate Layout = FEonformMeshTerrainOutput::EstimateLayout(FullResolution, Settings);
	if (!Layout.bValid)
	{
		BuildResult.Message = TEXT("The resolved Mesh Terrain regional layout is invalid.");
		return BuildResult;
	}

	const FEonformGridDomain ReferenceDomain = BaseContext.ResolveReferenceDomain(
		FullResolution,
		FVector2d(-50000.0, -50000.0),
		FVector2d(50000.0, 50000.0));
	if (!ReferenceDomain.IsValid())
	{
		BuildResult.Message = TEXT("Regional generation could not resolve the full-world reference domain.");
		return BuildResult;
	}

	TArray<FEonformTerrainRegionRequest> Requests;
	FString Error;
	if (!FEonformTerrainRegionPlanner::BuildRequests(
		FullResolution,
		ReferenceDomain.WorldMin,
		ReferenceDomain.WorldMax,
		Layout.Sections,
		RegionalEvaluationHalo,
		Requests,
		&Error))
	{
		BuildResult.Message = Error;
		return BuildResult;
	}

	AMeshPartition* Partition = ResolvePartition(World, Settings, Error);
	if (!Partition)
	{
		BuildResult.Message = Error;
		return BuildResult;
	}

	TSet<FIntPoint> UsedRegions;
	bool bAnySatMapColor = false;
	UMaterialInterface* SatMapDebugMaterial = nullptr;

	for (const FEonformTerrainRegionRequest& Request : Requests)
	{
		FEonformTerrainEvaluationContext RegionContext = BaseContext;
		RegionContext.TargetResolution = Request.Resolution;
		RegionContext.ReferenceResolution = FullResolution;
		RegionContext.Region = Request.EvaluationRegion;

		FEonformTerrainEvaluationResult RegionResult = FEonformTerrainEvaluator::Evaluate(Recipe, RegionContext);
		if (!RegionResult.bSuccess)
		{
			BuildResult.Message = FString::Printf(
				TEXT("Regional graph evaluation %d,%d failed: %s"),
				Request.RegionIndex.X,
				Request.RegionIndex.Y,
				*RegionResult.Error);
			return BuildResult;
		}

		FEonformTerrainMeshBuildOptions MeshOptions;
		MeshOptions.HeightScale = RegionResult.HeightScale;
		MeshOptions.HorizontalScale = FMath::Max(Settings.HorizontalScale, UE_DOUBLE_SMALL_NUMBER);
		MeshOptions.HorizontalScaleXY = FVector2d(
			FMath::Max(Settings.HorizontalScaleXY.X, UE_DOUBLE_SMALL_NUMBER),
			FMath::Max(Settings.HorizontalScaleXY.Y, UE_DOUBLE_SMALL_NUMBER));
		MeshOptions.VerticalScale = FMath::Max(Settings.VerticalScale, UE_DOUBLE_SMALL_NUMBER);
		MeshOptions.TargetResolution = Request.Resolution;

		FDynamicMesh3 RegionMesh;
		if (!FEonformTerrainMeshMaterializer::BuildDynamicMesh(
			RegionResult.Dataset,
			MeshOptions,
			RegionMesh,
			&Error))
		{
			BuildResult.Message = FString::Printf(
				TEXT("Regional mesh materialization %d,%d failed: %s"),
				Request.RegionIndex.X,
				Request.RegionIndex.Y,
				*Error);
			return BuildResult;
		}

		if (!bAnySatMapColor && HasSatMapColor(RegionResult.Dataset))
		{
			bAnySatMapColor = true;
			SatMapDebugMaterial = ResolveSatMapDebugMaterial();
		}

		const int32 RegionVertexCount = RegionMesh.VertexCount();
		const int32 RegionTriangleCount = RegionMesh.TriangleCount();
		AActor* RegionActor = EnsureRegionActor(World, Request.RegionIndex.X, Request.RegionIndex.Y, Error);
		if (!RegionActor)
		{
			BuildResult.Message = Error;
			return BuildResult;
		}

		RegionActor->Modify();
		USceneComponent* RegionRoot = EnsureRegionRoot(RegionActor);
		UMeshProviderModifier* Provider = EnsureRegionProvider(RegionActor, RegionRoot);
		if (!Provider)
		{
			BuildResult.Message = FString::Printf(
				TEXT("Failed to create the Mesh Provider for regional terrain %d,%d."),
				Request.RegionIndex.X,
				Request.RegionIndex.Y);
			return BuildResult;
		}

		Provider->Modify();
		Provider->BP_SetAffectedMegaMesh(Partition);
		if (SatMapDebugMaterial) Provider->SetMaterial(0, SatMapDebugMaterial);
		Provider->SetMesh(MoveTemp(RegionMesh), true);

		UsedRegions.Add(Request.RegionIndex);
		BuildResult.VertexCount += RegionVertexCount;
		BuildResult.TriangleCount += RegionTriangleCount;
		++BuildResult.SectionCount;

		// RegionResult.Dataset dies here. At no point do we retain the complete
		// final-resolution terrain raster in memory.
	}

	RemoveUnusedRegionActors(World, UsedRegions);
	BuildResult.bSuccess = true;
	BuildResult.TerrainActor = Partition;
	BuildResult.Message = FString::Printf(
		TEXT("Built EONFORM Mesh Terrain regionally: %d regions (%dx%d), %d vertices, %d triangles at virtual %dx%d samples. Peak terrain raster is one region plus halo, not the complete world.%s"),
		BuildResult.SectionCount,
		Layout.Sections.X,
		Layout.Sections.Y,
		BuildResult.VertexCount,
		BuildResult.TriangleCount,
		FullResolution.X,
		FullResolution.Y,
		bAnySatMapColor ? TEXT(" SatMap color preserved.") : TEXT(""));
	return BuildResult;
}
