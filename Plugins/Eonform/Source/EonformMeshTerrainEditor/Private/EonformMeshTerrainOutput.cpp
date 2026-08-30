#include "EonformMeshTerrainOutput.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EonformMeshTerrainBridgeActor.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionRegistry.h"
#include "Materials/MaterialInterface.h"
#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "Modifiers/MeshPartitionMeshProvider.h"
#include "UObject/UnrealType.h"

using UE::Geometry::FDynamicMesh3;
using UE::MeshPartition::AMeshPartition;
using UE::MeshPartition::UMeshPartitionDefinition;
using UE::MeshPartition::UMeshProviderModifier;

namespace
{
	const FName EonformBaseRegionTag(TEXT("EONFORM.MeshTerrain.BaseRegion"));
	const FString EonformRegionCoordinateTagPrefix(TEXT("EONFORM.MeshTerrain.Region."));
	const FString EonformRegionLabelPrefix(TEXT("EONFORM Base Region "));
	const FName EonformMeshTerrainFolder(TEXT("EONFORM/Mesh Terrain"));
	const FName EonformBaseRegionsFolder(TEXT("EONFORM/Mesh Terrain/Base Regions"));

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

	void ApplySatMapEditorMaterialOverride(AMeshPartition* Partition, UMaterialInterface* Material)
	{
		if (!Partition) return;

		UActorComponent* EditorComponent = nullptr;
		FObjectPropertyBase* OverrideProperty = nullptr;
		TInlineComponentArray<UActorComponent*> Components;
		Partition->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component) continue;
			FObjectPropertyBase* CandidateProperty = FindFProperty<FObjectPropertyBase>(
				Component->GetClass(),
				FName(TEXT("EditorMaterialOverride")));
			if (!CandidateProperty) continue;
			EditorComponent = Component;
			OverrideProperty = CandidateProperty;
			break;
		}

		if (!EditorComponent || !OverrideProperty)
		{
			UE_LOG(LogTemp, Warning, TEXT("EONFORM: Mesh Partition editor material override component/property was not found."));
			return;
		}

		EditorComponent->Modify();
		OverrideProperty->SetObjectPropertyValue_InContainer(EditorComponent, Material);
		if (UFunction* UpdateMaterialFunction = EditorComponent->FindFunction(FName(TEXT("UpdateMaterial"))))
		{
			EditorComponent->ProcessEvent(UpdateMaterialFunction, nullptr);
		}
		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(EditorComponent))
		{
			PrimitiveComponent->MarkRenderStateDirty();
		}
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

	bool ParseCoordinatePair(const FString& Text, TCHAR Separator, FIntPoint& OutCoordinate)
	{
		FString Left;
		FString Right;
		if (!Text.Split(FString::Chr(Separator), &Left, &Right)) return false;
		if (!Left.IsNumeric() || !Right.IsNumeric()) return false;
		OutCoordinate = FIntPoint(FCString::Atoi(*Left), FCString::Atoi(*Right));
		return OutCoordinate.X >= 0 && OutCoordinate.Y >= 0;
	}

	bool TryResolveRegionCoordinate(const AActor* Actor, FIntPoint& OutCoordinate)
	{
		if (!Actor || !Actor->Tags.Contains(EonformBaseRegionTag)) return false;
		for (const FName Tag : Actor->Tags)
		{
			const FString TagText = Tag.ToString();
			if (!TagText.StartsWith(EonformRegionCoordinateTagPrefix)) continue;
			if (ParseCoordinatePair(TagText.RightChop(EonformRegionCoordinateTagPrefix.Len()), TEXT('.'), OutCoordinate))
			{
				return true;
			}
		}
#if WITH_EDITOR
		const FString Label = Actor->GetActorLabel();
		if (Label.StartsWith(EonformRegionLabelPrefix))
		{
			return ParseCoordinatePair(Label.RightChop(EonformRegionLabelPrefix.Len()), TEXT(','), OutCoordinate);
		}
#endif
		return false;
	}

	void SetRegionCoordinate(AActor* Actor, const FIntPoint& Coordinate)
	{
		if (!Actor) return;
		for (int32 Index = Actor->Tags.Num() - 1; Index >= 0; --Index)
		{
			if (Actor->Tags[Index].ToString().StartsWith(EonformRegionCoordinateTagPrefix))
			{
				Actor->Tags.RemoveAt(Index);
			}
		}
		Actor->Tags.AddUnique(EonformBaseRegionTag);
		Actor->Tags.AddUnique(FName(*FString::Printf(
			TEXT("%s%d.%d"),
			*EonformRegionCoordinateTagPrefix,
			Coordinate.X,
			Coordinate.Y)));
#if WITH_EDITOR
		Actor->SetActorLabel(FString::Printf(TEXT("EONFORM Base Region %d,%d"), Coordinate.X, Coordinate.Y));
		Actor->SetFolderPath(EonformBaseRegionsFolder);
#endif
	}

	TMap<FIntPoint, AActor*> GatherRegionActors(UWorld* World)
	{
		TMap<FIntPoint, AActor*> Regions;
		if (!World) return Regions;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			FIntPoint Coordinate;
			if (TryResolveRegionCoordinate(Actor, Coordinate) && !Regions.Contains(Coordinate))
			{
				Regions.Add(Coordinate, Actor);
			}
		}
		return Regions;
	}

	AMeshPartition* ResolvePartition(UWorld* World, const FEonformMeshTerrainOutputSettings& Settings)
	{
		AMeshPartition* Partition = Cast<AMeshPartition>(Settings.TargetMeshPartition.Get());
		if (Partition) return Partition;
		for (TActorIterator<AMeshPartition> It(World); It; ++It)
		{
#if WITH_EDITOR
			if (It->GetActorLabel() == TEXT("EONFORM Mesh Terrain")) return *It;
#endif
		}
		return nullptr;
	}

	FEonformMeshTerrainBuildResult BuildInternal(
		UWorld* World,
		const FEonformTerrainDataset& Dataset,
		float HeightScale,
		const FEonformMeshTerrainOutputSettings& Settings,
		const TSet<FIntPoint>* RequestedRegions)
	{
		FEonformMeshTerrainBuildResult Result;
		const bool bPartialBuild = RequestedRegions != nullptr;
		if (!World)
		{
			Result.Message = TEXT("The editor world is not available.");
			return Result;
		}
		if (bPartialBuild && RequestedRegions->IsEmpty())
		{
			Result.Message = TEXT("No terrain regions were selected.");
			return Result;
		}

		FEonformTerrainMeshBuildOptions MeshOptions;
		MeshOptions.HeightScale = HeightScale;
		MeshOptions.HorizontalScale = FMath::Max(Settings.HorizontalScale, UE_DOUBLE_SMALL_NUMBER);
		MeshOptions.HorizontalScaleXY = FVector2d(
			FMath::Max(Settings.HorizontalScaleXY.X, UE_DOUBLE_SMALL_NUMBER),
			FMath::Max(Settings.HorizontalScaleXY.Y, UE_DOUBLE_SMALL_NUMBER));
		MeshOptions.VerticalScale = FMath::Max(Settings.VerticalScale, UE_DOUBLE_SMALL_NUMBER);
		MeshOptions.TargetResolution = Settings.TargetResolution;

		const FIntPoint Resolution = FEonformTerrainMeshMaterializer::ResolveTargetResolution(Dataset, MeshOptions);
		const FEonformMeshTerrainLayoutEstimate Layout = FEonformMeshTerrainOutput::EstimateLayout(Resolution, Settings);
		if (!Layout.bValid)
		{
			Result.Message = TEXT("The resolved Mesh Terrain output resolution must be at least 2x2 samples.");
			return Result;
		}

		const FIntPoint Sections = Layout.Sections;
		if (bPartialBuild)
		{
			for (const FIntPoint& Coordinate : *RequestedRegions)
			{
				if (Coordinate.X < 0 || Coordinate.Y < 0 || Coordinate.X >= Sections.X || Coordinate.Y >= Sections.Y)
				{
					Result.Message = FString::Printf(
						TEXT("Selected region %d,%d is outside the current %dx%d region grid."),
						Coordinate.X,
						Coordinate.Y,
						Sections.X,
						Sections.Y);
					return Result;
				}
			}
		}

		const bool bHasSatMapColor = HasSatMapColor(Dataset);
		UMaterialInterface* SatMapDebugMaterial = bHasSatMapColor ? ResolveSatMapDebugMaterial() : nullptr;
		FString RegionTrackingReason;
		const bool bTrackRegions = FEonformTerrainRegionRegistry::BeginCurrentPlan(Resolution, Sections, &RegionTrackingReason);

		const int32 IntervalsX = Resolution.X - 1;
		const int32 IntervalsY = Resolution.Y - 1;
		if (bTrackRegions)
		{
			for (int32 SectionY = 0; SectionY < Sections.Y; ++SectionY)
			{
				const int32 StartY = (SectionY * IntervalsY) / Sections.Y;
				const int32 EndY = ((SectionY + 1) * IntervalsY) / Sections.Y;
				for (int32 SectionX = 0; SectionX < Sections.X; ++SectionX)
				{
					const int32 StartX = (SectionX * IntervalsX) / Sections.X;
					const int32 EndX = ((SectionX + 1) * IntervalsX) / Sections.X;
					FEonformTerrainRegionRegistry::RegisterCurrentRegion(
						FIntPoint(SectionX, SectionY),
						FIntPoint(StartX, StartY),
						FIntPoint(EndX, EndY));
				}
			}
		}

		AMeshPartition* Partition = ResolvePartition(World, Settings);
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

		if (!bPartialBuild)
		{
			for (TActorIterator<AEonformMeshTerrainBridgeActor> It(World); It; ++It)
			{
				AEonformMeshTerrainBridgeActor* Bridge = *It;
				if (!Bridge || Bridge->TargetMeshPartition.Get() != Partition) continue;
				if (UMeshProviderModifier* BridgeProvider = Cast<UMeshProviderModifier>(Bridge->MeshProviderComponent.Get()))
				{
					ClearProviderMesh(BridgeProvider);
				}
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
		}

		TMap<FIntPoint, AActor*> ExistingRegions = GatherRegionActors(World);
		for (int32 SectionY = 0; SectionY < Sections.Y; ++SectionY)
		{
			const int32 StartY = (SectionY * IntervalsY) / Sections.Y;
			const int32 EndY = ((SectionY + 1) * IntervalsY) / Sections.Y;
			for (int32 SectionX = 0; SectionX < Sections.X; ++SectionX)
			{
				const FIntPoint Coordinate(SectionX, SectionY);
				if (bPartialBuild && !RequestedRegions->Contains(Coordinate)) continue;

				const int32 StartX = (SectionX * IntervalsX) / Sections.X;
				const int32 EndX = ((SectionX + 1) * IntervalsX) / Sections.X;
				const FIntPoint StartSample(StartX, StartY);
				const FIntPoint EndSample(EndX, EndY);
				if (bTrackRegions) FEonformTerrainRegionRegistry::BeginEvaluation(StartSample, EndSample);

				FDynamicMesh3 SectionMesh;
				FString MaterializeError;
				if (!FEonformTerrainMeshMaterializer::BuildDynamicMeshRegion(
					Dataset,
					MeshOptions,
					StartSample,
					EndSample,
					SectionMesh,
					&MaterializeError))
				{
					if (bTrackRegions) FEonformTerrainRegionRegistry::FailEvaluation(StartSample, EndSample, MaterializeError);
					Result.Message = FString::Printf(
						TEXT("Mesh Terrain base region %d,%d failed: %s"),
						SectionX,
						SectionY,
						*MaterializeError);
					return Result;
				}

				const int32 RegionVertexCount = SectionMesh.VertexCount();
				const int32 RegionTriangleCount = SectionMesh.TriangleCount();
				if (bTrackRegions)
				{
					FEonformTerrainRegionRegistry::CompleteEvaluation(
						StartSample,
						EndSample,
						RegionVertexCount,
						RegionTriangleCount);
				}

				AActor* RegionActor = ExistingRegions.FindRef(Coordinate);
				if (!RegionActor)
				{
					FActorSpawnParameters RegionSpawnParameters;
					RegionSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					RegionSpawnParameters.ObjectFlags |= RF_Transactional;
					RegionActor = World->SpawnActor<AActor>(
						AActor::StaticClass(),
						FVector::ZeroVector,
						FRotator::ZeroRotator,
						RegionSpawnParameters);
					if (!RegionActor)
					{
						const FString Error = FString::Printf(
							TEXT("Failed to create EONFORM Mesh Terrain base region actor %d,%d."),
							SectionX,
							SectionY);
						if (bTrackRegions) FEonformTerrainRegionRegistry::FailMaterialization(StartSample, EndSample, Error);
						Result.Message = Error;
						return Result;
					}
					ExistingRegions.Add(Coordinate, RegionActor);
				}

				RegionActor->Modify();
				SetRegionCoordinate(RegionActor, Coordinate);
				USceneComponent* RegionRoot = EnsureRegionRoot(RegionActor);
				if (!RegionRoot)
				{
					const FString Error = TEXT("Failed to create or resolve an EONFORM base region scene root.");
					if (bTrackRegions) FEonformTerrainRegionRegistry::FailMaterialization(StartSample, EndSample, Error);
					Result.Message = Error;
					return Result;
				}
				UMeshProviderModifier* MeshProvider = EnsureRegionProvider(RegionActor, RegionRoot);
				if (!MeshProvider)
				{
					const FString Error = TEXT("Failed to create or resolve the UE 5.8 Mesh Provider modifier for an EONFORM base region.");
					if (bTrackRegions) FEonformTerrainRegionRegistry::FailMaterialization(StartSample, EndSample, Error);
					Result.Message = Error;
					return Result;
				}

				if (bTrackRegions) FEonformTerrainRegionRegistry::BeginCommit(StartSample, EndSample);
				MeshProvider->Modify();
				MeshProvider->BP_SetAffectedMegaMesh(Partition);
				if (SatMapDebugMaterial) MeshProvider->SetMaterial(0, SatMapDebugMaterial);
				MeshProvider->SetMesh(MoveTemp(SectionMesh), true);
				if (bTrackRegions) FEonformTerrainRegionRegistry::MarkLoaded(StartSample, EndSample);

				Result.VertexCount += RegionVertexCount;
				Result.TriangleCount += RegionTriangleCount;
				++Result.SectionCount;
			}
		}

		if (!bPartialBuild)
		{
			for (const TPair<FIntPoint, AActor*>& Pair : ExistingRegions)
			{
				const FIntPoint Coordinate = Pair.Key;
				if (Coordinate.X >= 0 && Coordinate.Y >= 0 && Coordinate.X < Sections.X && Coordinate.Y < Sections.Y) continue;
				AActor* ExtraActor = Pair.Value;
				if (!ExtraActor) continue;
				TInlineComponentArray<UMeshProviderModifier*> ExtraProviders;
				ExtraActor->GetComponents(ExtraProviders);
				for (UMeshProviderModifier* Provider : ExtraProviders) ClearProviderMesh(Provider);
				ExtraActor->Modify();
				ExtraActor->Destroy();
			}
		}

		ApplySatMapEditorMaterialOverride(Partition, SatMapDebugMaterial);
		Result.bSuccess = true;
		Result.TerrainActor = Partition;
		if (bPartialBuild)
		{
			Result.Message = FString::Printf(
				TEXT("Rebuilt %d selected EONFORM terrain region%s: %d vertices, %d triangles."),
				Result.SectionCount,
				Result.SectionCount == 1 ? TEXT("") : TEXT("s"),
				Result.VertexCount,
				Result.TriangleCount);
		}
		else
		{
			Result.Message = FString::Printf(
				TEXT("Built EONFORM Mesh Terrain: %d base regions (%dx%d), %d vertices, %d triangles at %dx%d samples; max region %dx%d / %lld triangles (XY x%.3f/x%.3f, Z x%.3f)%s."),
				Result.SectionCount,
				Sections.X,
				Sections.Y,
				Result.VertexCount,
				Result.TriangleCount,
				Resolution.X,
				Resolution.Y,
				Layout.MaxSectionResolution.X,
				Layout.MaxSectionResolution.Y,
				static_cast<long long>(Layout.MaxSectionTriangleCount),
				MeshOptions.HorizontalScale * MeshOptions.HorizontalScaleXY.X,
				MeshOptions.HorizontalScale * MeshOptions.HorizontalScaleXY.Y,
				MeshOptions.VerticalScale,
				bHasSatMapColor ? TEXT(" with SatMap editor color") : TEXT(""));
		}
		return Result;
	}
}

UClass* FEonformMeshTerrainOutput::GetMeshPartitionDefinitionClass()
{
	return UMeshPartitionDefinition::StaticClass();
}

int32 FEonformMeshTerrainOutput::GetTargetTrianglesPerSection(EEonformMeshTerrainSectionComplexity Complexity)
{
	switch (Complexity)
	{
	case EEonformMeshTerrainSectionComplexity::Responsive: return 32768;
	case EEonformMeshTerrainSectionComplexity::Detailed: return 524288;
	case EEonformMeshTerrainSectionComplexity::Maximum: return 2097152;
	case EEonformMeshTerrainSectionComplexity::Balanced:
	default: return 131072;
	}
}

FEonformMeshTerrainLayoutEstimate FEonformMeshTerrainOutput::EstimateLayout(
	const FIntPoint& Resolution,
	const FEonformMeshTerrainOutputSettings& Settings)
{
	FEonformMeshTerrainLayoutEstimate Estimate;
	Estimate.Resolution = Resolution;
	if (Resolution.X < 2 || Resolution.Y < 2) return Estimate;

	const int32 IntervalsX = Resolution.X - 1;
	const int32 IntervalsY = Resolution.Y - 1;
	const int32 MaxSectionsX = FMath::Max(1, IntervalsX);
	const int32 MaxSectionsY = FMath::Max(1, IntervalsY);
	FIntPoint Sections(1, 1);
	if (Settings.SectionLayout == EEonformMeshTerrainSectionLayout::Explicit)
	{
		Sections.X = FMath::Clamp(Settings.Sections.X, 1, MaxSectionsX);
		Sections.Y = FMath::Clamp(Settings.Sections.Y, 1, MaxSectionsY);
	}
	else
	{
		Estimate.TargetTrianglesPerSection = GetTargetTrianglesPerSection(Settings.SectionComplexity);
		const int32 TargetQuadsPerAxis = FMath::Max(
			1,
			FMath::FloorToInt(FMath::Sqrt(static_cast<double>(Estimate.TargetTrianglesPerSection) * 0.5)));
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

FEonformMeshTerrainBuildResult FEonformMeshTerrainOutput::Build(
	UWorld* World,
	const FEonformTerrainDataset& Dataset,
	float HeightScale,
	const FEonformMeshTerrainOutputSettings& Settings)
{
	return BuildInternal(World, Dataset, HeightScale, Settings, nullptr);
}

FEonformMeshTerrainBuildResult FEonformMeshTerrainOutput::BuildRegions(
	UWorld* World,
	const FEonformTerrainDataset& Dataset,
	float HeightScale,
	const FEonformMeshTerrainOutputSettings& Settings,
	const TArray<FIntPoint>& RegionIndices)
{
	TSet<FIntPoint> RequestedRegions;
	for (const FIntPoint& Coordinate : RegionIndices) RequestedRegions.Add(Coordinate);
	return BuildInternal(World, Dataset, HeightScale, Settings, &RequestedRegions);
}

int32 FEonformMeshTerrainOutput::UnloadRegions(
	UWorld* World,
	const TArray<FIntPoint>& RegionIndices,
	FString* OutError)
{
	if (!World)
	{
		if (OutError) *OutError = TEXT("The editor world is not available.");
		return 0;
	}

	TSet<FIntPoint> Requested;
	for (const FIntPoint& Coordinate : RegionIndices) Requested.Add(Coordinate);
	const TMap<FIntPoint, AActor*> ExistingRegions = GatherRegionActors(World);

	FEonformTerrainRegionPlanIdentity Identity;
	TArray<FEonformTerrainRegionSnapshot> Snapshots;
	FEonformTerrainRegionRegistry::GetLatestPlan(Identity, Snapshots);

	int32 Unloaded = 0;
	for (const FIntPoint& Coordinate : Requested)
	{
		AActor* RegionActor = ExistingRegions.FindRef(Coordinate);
		if (!RegionActor) continue;

		const FEonformTerrainRegionSnapshot* Snapshot = Snapshots.FindByPredicate(
			[&Coordinate](const FEonformTerrainRegionSnapshot& Candidate)
			{
				return Candidate.RegionIndex == Coordinate;
			});
		if (Snapshot)
		{
			FEonformTerrainRegionRegistry::BeginEviction(Snapshot->Key.StartSample, Snapshot->Key.EndSample);
		}

		TInlineComponentArray<UMeshProviderModifier*> Providers;
		RegionActor->GetComponents(Providers);
		for (UMeshProviderModifier* Provider : Providers) ClearProviderMesh(Provider);
		RegionActor->Modify();
		RegionActor->Destroy();

		if (Snapshot)
		{
			FEonformTerrainRegionRegistry::MarkUnloaded(Snapshot->Key.StartSample, Snapshot->Key.EndSample);
		}
		++Unloaded;
	}

	if (OutError) OutError->Reset();
	return Unloaded;
}

AActor* FEonformMeshTerrainOutput::FindRegionActor(UWorld* World, const FIntPoint& RegionIndex)
{
	return GatherRegionActors(World).FindRef(RegionIndex);
}

bool FEonformMeshTerrainOutput::TryGetRegionIndex(const AActor* Actor, FIntPoint& OutRegionIndex)
{
	return TryResolveRegionCoordinate(Actor, OutRegionIndex);
}
