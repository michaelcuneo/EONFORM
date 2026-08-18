#include "TerrainGeneratorActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "TerrainHeightField.h"

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

float ATerrainGeneratorActor::SampleFractalNoise(float WorldX, float WorldY) const
{
	const FRandomStream RandomStream(Seed);
	const FVector2D SeedOffset(
		RandomStream.FRandRange(-100000.0f, 100000.0f),
		RandomStream.FRandRange(-100000.0f, 100000.0f));

	float Amplitude = 1.0f;
	float LocalFrequency = Frequency;
	float Value = 0.0f;
	float AmplitudeSum = 0.0f;

	for (int32 Octave = 0; Octave < Octaves; ++Octave)
	{
		const FVector2D SamplePosition(
			(WorldX + SeedOffset.X) * LocalFrequency,
			(WorldY + SeedOffset.Y) * LocalFrequency);

		Value += FMath::PerlinNoise2D(SamplePosition) * Amplitude;
		AmplitudeSum += Amplitude;

		Amplitude *= Persistence;
		LocalFrequency *= Lacunarity;
	}

	return AmplitudeSum > UE_SMALL_NUMBER ? Value / AmplitudeSum : 0.0f;
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

	for (int32 Y = 0; Y < SafeResolution; ++Y)
	{
		for (int32 X = 0; X < SafeResolution; ++X)
		{
			const float LocalX = static_cast<float>(X) * CellSize - HalfWorldSize;
			const float LocalY = static_cast<float>(Y) * CellSize - HalfWorldSize;
			HeightField.At(X, Y) = SampleFractalNoise(LocalX, LocalY);
		}
	}

	FDynamicMesh3 Mesh;
	Mesh.EnableVertexNormals(FVector3f::UpVector);

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

			Mesh.AppendTriangle(A, D, B);
			Mesh.AppendTriangle(A, C, D);
		}
	}

	FMeshNormals::QuickComputeVertexNormals(Mesh);
	TerrainMesh->SetMesh(MoveTemp(Mesh));
}
