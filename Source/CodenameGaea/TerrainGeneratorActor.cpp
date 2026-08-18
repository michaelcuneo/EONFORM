#include "TerrainGeneratorActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "TerrainHeightField.h"
#include "TerrainNoise.h"
#include "TerrainShaping.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FMeshNormals;

ATerrainGeneratorActor::ATerrainGeneratorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("TerrainMesh"));
	SetRootComponent(TerrainMesh);

	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainMesh->SetGenerateOverlapEvents(false);
	TerrainMesh->SetCastShadow(true);
}

void ATerrainGeneratorActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildTerrain();
}

#if WITH_EDITOR
void ATerrainGeneratorActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	BuildTerrain();
}
#endif

void ATerrainGeneratorActor::Regenerate()
{
	BuildTerrain();
}

void ATerrainGeneratorActor::RandomizeSeed()
{
	Seed = FMath::Rand();
	BuildTerrain();
}

void ATerrainGeneratorActor::BuildTerrain()
{
	if (!TerrainMesh)
	{
		return;
	}

	const int32 SafeResolution = FMath::Clamp(Resolution, 2, 1025);
	const float SafeWorldSize = FMath::Max(WorldSize, 1.0f);

	FTerrainHeightField HeightField;
	HeightField.Initialize(SafeResolution, SafeWorldSize);

	const float CellSize = SafeWorldSize / static_cast<float>(SafeResolution - 1);
	const float HalfWorldSize = SafeWorldSize * 0.5f;

	FTerrainFractalNoiseSettings BaseSettings;
	BaseSettings.Frequency = Frequency;
	BaseSettings.Octaves = Octaves;
	BaseSettings.Persistence = Persistence;
	BaseSettings.Lacunarity = Lacunarity;

	FTerrainFractalNoiseSettings MacroSettings;
	MacroSettings.Frequency = MacroFrequency;
	MacroSettings.Octaves = MacroOctaves;
	MacroSettings.Persistence = 0.5f;
	MacroSettings.Lacunarity = 2.0f;

	FTerrainFractalNoiseSettings WarpSettings;
	WarpSettings.Frequency = WarpFrequency;
	WarpSettings.Octaves = 3;
	WarpSettings.Persistence = 0.5f;
	WarpSettings.Lacunarity = 2.0f;

	FTerrainFractalNoiseSettings RidgeSettings;
	RidgeSettings.Frequency = RidgeFrequency;
	RidgeSettings.Octaves = RidgeOctaves;
	RidgeSettings.Persistence = Persistence;
	RidgeSettings.Lacunarity = Lacunarity;

	const FVector2D BaseOffset = FTerrainNoise::MakeSeedOffset(Seed, 0);
	const FVector2D MacroOffset = FTerrainNoise::MakeSeedOffset(Seed, 17);
	const FVector2D WarpXOffset = FTerrainNoise::MakeSeedOffset(Seed, 101);
	const FVector2D WarpYOffset = FTerrainNoise::MakeSeedOffset(Seed, 202);
	const FVector2D RidgeOffset = FTerrainNoise::MakeSeedOffset(Seed, 303);

	for (int32 Y = 0; Y < SafeResolution; ++Y)
	{
		for (int32 X = 0; X < SafeResolution; ++X)
		{
			const FVector2D WorldPosition(
				static_cast<float>(X) * CellSize - HalfWorldSize,
				static_cast<float>(Y) * CellSize - HalfWorldSize);

			FVector2D SamplePosition = WorldPosition;

			if (bEnableDomainWarp && WarpStrength > 0.0f)
			{
				const float WarpX = FTerrainNoise::SampleFractal(WorldPosition, WarpXOffset, WarpSettings);
				const float WarpY = FTerrainNoise::SampleFractal(WorldPosition, WarpYOffset, WarpSettings);
				SamplePosition += FVector2D(WarpX, WarpY) * WarpStrength;
			}

			const float BaseHeight = FTerrainNoise::SampleFractal(SamplePosition, BaseOffset, BaseSettings);
			float MacroHeight = 0.0f;
			float MountainMask = 1.0f;

			if (bEnableMacroShape)
			{
				MacroHeight = FTerrainNoise::SampleFractal(SamplePosition, MacroOffset, MacroSettings);
				MacroHeight = FTerrainShaping::ApplySignedPower(MacroHeight, MacroContrast);

				if (bEnableMountainMask)
				{
					MountainMask = FTerrainShaping::BuildMountainMask(
						MacroHeight,
						MountainThreshold,
						MountainTransition);
				}
			}

			float Height = BaseHeight * 0.45f;

			if (bEnableMacroShape)
			{
				Height += MacroHeight * MacroStrength;
			}

			if (bEnableRidges && RidgeStrength > 0.0f)
			{
				const float Ridge = FTerrainNoise::SampleRidged(SamplePosition, RidgeOffset, RidgeSettings, RidgeSharpness);
				const float SignedRidge = Ridge * 2.0f - 1.0f;
				Height += SignedRidge * RidgeStrength * MountainMask;
			}

			if (bEnableLowlandFlattening && LowlandStrength > 0.0f)
			{
				const float LowlandMask = 1.0f - MountainMask;
				const float Flattened = FTerrainShaping::ApplySignedPower(Height, LowlandExponent);
				Height = FMath::Lerp(Height, Flattened, LowlandMask * LowlandStrength);
			}

			HeightField.At(X, Y) = FMath::Clamp(Height, -1.0f, 1.0f);
		}
	}

	FDynamicMesh3 Mesh(true, false, false, false);

	TArray<int32> VertexIds;
	VertexIds.SetNumUninitialized(SafeResolution * SafeResolution);

	for (int32 Y = 0; Y < SafeResolution; ++Y)
	{
		for (int32 X = 0; X < SafeResolution; ++X)
		{
			const float LocalX = static_cast<float>(X) * CellSize - HalfWorldSize;
			const float LocalY = static_cast<float>(Y) * CellSize - HalfWorldSize;
			const float Height = HeightField.At(X, Y) * HeightScale;
			const float LocalZ = bCenterHeightfield ? Height : Height + HeightScale;

			const int32 VertexId = Mesh.AppendVertex(FVector3d(LocalX, LocalY, LocalZ));
			VertexIds[Y * SafeResolution + X] = VertexId;
		}
	}

	for (int32 Y = 0; Y < SafeResolution - 1; ++Y)
	{
		for (int32 X = 0; X < SafeResolution - 1; ++X)
		{
			const int32 A = VertexIds[Y * SafeResolution + X];
			const int32 B = VertexIds[Y * SafeResolution + X + 1];
			const int32 C = VertexIds[(Y + 1) * SafeResolution + X];
			const int32 D = VertexIds[(Y + 1) * SafeResolution + X + 1];

			Mesh.AppendTriangle(A, D, B, 0);
			Mesh.AppendTriangle(A, C, D, 0);
		}
	}

	FMeshNormals::QuickComputeVertexNormals(Mesh);
	TerrainMesh->SetMesh(MoveTemp(Mesh));
}
