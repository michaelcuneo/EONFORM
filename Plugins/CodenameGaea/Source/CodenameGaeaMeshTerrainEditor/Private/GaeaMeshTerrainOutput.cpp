#include "GaeaMeshTerrainOutput.h"

#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GaeaMeshTerrainBridgeActor.h"
#include "GaeaTerrainFieldNames.h"
#include "Materials/MaterialInterface.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "MeshPartitionEditorComponent.h"
#include "Modifiers/MeshPartitionMeshProvider.h"
#include "UObject/UnrealType.h"

using UE::Geometry::FDynamicMesh3;
using UE::MeshPartition::AMeshPartition;
using UE::MeshPartition::UMeshPartitionDefinition;
using UE::MeshPartition::UMeshProviderModifier;

namespace
{
	const FName EonformBaseRegionTag(TEXT("EONFORM.MeshTerrain.BaseRegion"));
	const FName EonformMeshTerrainFolder(TEXT("EONFORM/Mesh Terrain"));
	const FName EonformBaseRegionsFolder(TEXT("EONFORM/Mesh Terrain/Base Regions"));

	bool HasSatMapColor(const FGaeaTerrainDataset& Dataset)
	{
		const FGaeaScalarField* R = Dataset.FindScalarField(GaeaTerrainFieldNames::BaseColorR);
		const FGaeaScalarField* G = Dataset.FindScalarField(GaeaTerrainFieldNames::BaseColorG);
		const FGaeaScalarField* B = Dataset.FindScalarField(GaeaTerrainFieldNames::BaseColorB);
		return R && G && B && R->IsValid() && G->IsValid() && B->IsValid();
	}

	UMaterialInterface* ResolveSatMapDebugMaterial()
	{
		if (!GEngine) return nullptr;
		if (GEngine->VertexColorMaterial) return GEngine->VertexColorMaterial;
		return GEngine->VertexColorViewModeMaterial_ColorOnly;
	}

	void ApplySatMapEditorMaterialOverride(AMeshPartition* Partition, UMaterialInterface* Material)
	{
		if (!Partition) return;
		UMeshPartitionEditorComponent* EditorComponent = Partition->FindComponentByClass<UMeshPartitionEditorComponent>();
		if (!EditorComponent)
		{
			UE_LOG(LogTemp, Warning, TEXT("EONFORM: Mesh Partition has no UMeshPartitionEditorComponent; SatMap editor material override was not applied."));
			return;
		}

		FObjectPropertyBase* OverrideProperty = FindFProperty<FObjectPropertyBase>(
			UMeshPartitionEditorComponent::StaticClass(),
			FName(TEXT("EditorMaterialOverride")));
		if (!OverrideProperty)
		{
			UE_LOG(LogTemp, Warning, TEXT("EONFORM: UE 5.8 Mesh Partition editor material override property was not found."));
			return;
		}

		EditorComponent->Modify();
		OverrideProperty->SetObjectPropertyValue_InContainer(EditorComponent, Material);
		EditorComponent->UpdateMaterial();
		EditorComponent->MarkRenderStateDirty();
	}

	void ClearProviderMesh(UMeshProviderModifier* Provider)
	{
		if (!Provider) return;
		Provider->Modify();
		FDynamicMesh3 EmptyMesh;
		Provider->SetMesh(MoveTemp(EmptyMesh), true);
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
		UMeshProviderModifier* MeshProvider = NewObject<UMeshProviderModifier>(RegionActor, NAME_None, RF_Transactional);
		if (!MeshProvider) return nullptr;
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

int32 FGaeaMeshTerrainOutput::GetTargetTrianglesPerSection(EGaeaMeshTerrainSectionComplexity Complexity)
{
	switch (Complexity)
	{
	case EGaeaMeshTerrainSectionComplexity::Responsive: return 32768;
	case EGaeaMeshTerrainSectionComplexity::Detailed: return 524288;
	case EGaeaMeshTerrainSectionComplexity::Maximum: return 2097152;
	case EGaeaMeshTerrainSectionComplexity::Balanced:
	default: return 131072;
	}
}

FGaeaMeshTerrainLayoutEstimate FGaeaMeshTerrainOutput::EstimateLayout(
	const FIntPoint& Resolution,
	const FGaeaMeshTerrainOutputSettings& Settings)
{
	FGaeaMeshTerrainLayoutEstimate Estimate;
	Estimate.Resolution = Resolution;
	if (Resolution.X < 2 || Resolution.Y < 2) return Estimate;

	const int32 IntervalsX = Resolution.X - 1;
	const int32 IntervalsY = Resolution.Y - 1;
	const int32 MaxSectionsX = FMath::Max(1, IntervalsX);
	const int32 MaxSectionsY = FMath::Max(1, IntervalsY);

	FIntPoint Sections(1, 1);
	if (Settings.SectionLayout == EGaeaMeshTerrainSectionLayout::Explicit)
	{
		Sections.X = FMath::Clamp(Settings.Sections.X, 1, MaxSectionsX);
		Sections.Y = FMath::Clamp(Settings.Sections.Y, 1, MaxSectionsY);
	}
	else
	{
		Estimate.TargetTrianglesPerSection = GetTargetTrianglesPerSection(Settings.SectionComplexity);
		const int32 TargetQuadsPerAxis = FMath::Max(1, FMath::FloorToInt(FMath::Sqrt(static_cast<double>(Estimate.TargetTrianglesPerSection) * 0.5)));
		Sections.X = FMath::Clamp(FMath::DivideAndRoundUp(IntervalsX, TargetQuadsPerAxis), 1, MaxSectionsX);
		Sections.Y = FMath::Clamp(FMath::DivideAndRoundUp(IntervalsY, TargetQuadsPerAxis), 1, MaxSectionsY);
	}

	const int32 MaxIntervalsPerSectionX = FMath::DivideAndRoundUp(IntervalsX, Sections.X);
	const int32 MaxIntervalsPerSectionY = FMath::DivideAndRoundUp(IntervalsY, Sections.Y);
	Estimate.bValid = true;
	Estimate.Sections = Sections;
	Estimate.MaxSectionResolution = FIntPoint(MaxIntervalsPerSectionX + 1, MaxIntervalsPerSectionY + 1);
	Estimate.SectionCount = static_cast<int64>(Sections.X) * static_cast<int64>(Sections.Y);
	Estimate.TotalTriangleCount = static_cast<int64>(IntervalsX) * static_cast<int64>(IntervalsY) * 2ll;
	Estimate.TotalVertexCount = static_cast<int64>(IntervalsX + Sections.X) * static_cast<int64>(IntervalsY + Sections.Y);
	Estimate.MaxSectionTriangleCount = static_cast<int64>(MaxIntervalsPerSectionX) * static_cast<int64>(MaxIntervalsPerSectionY) * 2ll;
	Estimate.MaxSectionVertexCount = static_cast<int64>(MaxIntervalsPerSectionX + 1) * static_cast<int64>(MaxIntervalsPerSectionY + 1);
	return Estimate;
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
	MeshOptions.HorizontalScaleXY = FVector2d(
		FMath::Max(Settings.HorizontalScaleXY.X, UE_DOUBLE_SMALL_NUMBER),
		FMath::Max(Settings.HorizontalScaleXY.Y, UE_DOUBLE_SMALL_NUMBER));
	MeshOptions.VerticalScale = FMath::Max(Settings.VerticalScale, UE_DOUBLE_SMALL_NUMBER);
	MeshOptions.TargetResolution = Settings.TargetResolution;

	const FIntPoint Resolution = FGaeaTerrainMeshMaterializer::ResolveTargetResolution(Dataset, MeshOptions);
	const FGaeaMeshTerrainLayoutEstimate Layout = EstimateLayout(Resolution, Settings);
	if (!Layout.bValid)
	{
		Result.Message = TEXT("The resolved Mesh Terrain output resolution must be at least 2x2 samples.");
		return Result;
	}

	const FIntPoint Sections = Layout.Sections;
	const int32 RequiredRegionCount = static_cast<int32>(Layout.SectionCount);
	const bool bHasSatMapColor = HasSatMapColor(Dataset);
	UMaterialInterface* SatMapDebugMaterial = bHasSatMapColor ? ResolveSatMapDebugMaterial() : nullptr;

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
			Result.Message = TEXT("UE 5.8 failed to create the EONFORM Mesh Partition actor.");
			return Result;
		}
#if WITH_EDITOR
		Partition->SetActorLabel(TEXT("EONFORM Mesh Terrain"));
#endif
	}

	UMeshPartitionDefinition* Definition = Cast<UMeshPartitionDefinition>(Settings.MeshPartitionDefinition.Get());
	if (!Definition) Definition = Partition->GetMeshPartitionDefinition();
	if (!Definition)
	{
		Result.Message = TEXT("Assign a UE 5.8 Mesh Partition Definition in EONFORM Terrain Output before generating terrain.");
		return Result;
	}

	Partition->Modify();
	Partition->SetMeshPartitionDefinition(Definition);
	ApplySatMapEditorMaterialOverride(Partition, SatMapDebugMaterial);
#if WITH_EDITOR
	Partition->SetFolderPath(EonformMeshTerrainFolder);
#endif

	for (TActorIterator<AGaeaMeshTerrainBridgeActor> It(World); It; ++It)
	{
		AGaeaMeshTerrainBridgeActor* Bridge = *It;
		if (!Bridge || Bridge->TargetMeshPartition.Get() != Partition) continue;
		if (UMeshProviderModifier* BridgeProvider = Cast<UMeshProviderModifier>(Bridge->MeshProviderComponent.Get())) ClearProviderMesh(BridgeProvider);
	}

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

	TArray<AActor*> ExistingBaseRegions;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->Tags.Contains(EonformBaseRegionTag)) ExistingBaseRegions.Add(Actor);
	}
#if WITH_EDITOR
	ExistingBaseRegions.Sort([](const AActor& A, const AActor& B) { return A.GetActorLabel() < B.GetActorLabel(); });
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
			if (!FGaeaTerrainMeshMaterializer::BuildDynamicMeshRegion(Dataset, MeshOptions, FIntPoint(StartX, StartY), FIntPoint(EndX, EndY), SectionMesh, &MaterializeError))
			{
				Result.Message = FString::Printf(TEXT("Mesh Terrain base region %d,%d failed: %s"), SectionX, SectionY, *MaterializeError);
				return Result;
			}

			const int32 RegionVertexCount = SectionMesh.VertexCount();
			const int32 RegionTriangleCount = SectionMesh.TriangleCount();
			AActor* RegionActor = ExistingBaseRegions.IsValidIndex(RegionIndex) ? ExistingBaseRegions[RegionIndex] : nullptr;
			if (!RegionActor)
			{
				FActorSpawnParameters RegionSpawnParameters;
				RegionSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				RegionSpawnParameters.ObjectFlags |= RF_Transactional;
				RegionActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, RegionSpawnParameters);
				if (!RegionActor)
				{
					Result.Message = FString::Printf(TEXT("Failed to create EONFORM Mesh Terrain base region actor %d,%d."), SectionX, SectionY);
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
			if (SatMapDebugMaterial)
			{
				MeshProvider->SetMaterial(0, SatMapDebugMaterial);
			}
			MeshProvider->SetMesh(MoveTemp(SectionMesh), true);
			Result.VertexCount += RegionVertexCount;
			Result.TriangleCount += RegionTriangleCount;
			++Result.SectionCount;
		}
	}

	for (int32 ExtraIndex = RequiredRegionCount; ExtraIndex < ExistingBaseRegions.Num(); ++ExtraIndex)
	{
		AActor* ExtraActor = ExistingBaseRegions[ExtraIndex];
		if (!ExtraActor) continue;
		TInlineComponentArray<UMeshProviderModifier*> ExtraProviders;
		ExtraActor->GetComponents(ExtraProviders);
		for (UMeshProviderModifier* Provider : ExtraProviders) ClearProviderMesh(Provider);
		ExtraActor->Modify();
		ExtraActor->Destroy();
	}

	// SetMesh can create/rebuild preview sections asynchronously. Refresh the
	// editor override once more after all base modifiers have been updated.
	ApplySatMapEditorMaterialOverride(Partition, SatMapDebugMaterial);

	Result.bSuccess = true;
	Result.TerrainActor = Partition;
	Result.Message = FString::Printf(
		TEXT("Built EONFORM Mesh Terrain: %d base regions (%dx%d), %d vertices, %d triangles at %dx%d samples; max region %dx%d / %lld triangles (XY x%.3f/x%.3f, Z x%.3f)%s."),
		Result.SectionCount, Sections.X, Sections.Y, Result.VertexCount, Result.TriangleCount, Resolution.X, Resolution.Y,
		Layout.MaxSectionResolution.X, Layout.MaxSectionResolution.Y, static_cast<long long>(Layout.MaxSectionTriangleCount),
		MeshOptions.HorizontalScale * MeshOptions.HorizontalScaleXY.X,
		MeshOptions.HorizontalScale * MeshOptions.HorizontalScaleXY.Y,
		MeshOptions.VerticalScale,
		bHasSatMapColor ? TEXT(" with SatMap editor color") : TEXT(""));
	return Result;
}
