#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainMeshMaterializer.h"
#include "EonformTerrainDynamicMeshActor.generated.h"

class UDynamicMeshComponent;

/** Runtime-safe scene actor that renders a EONFORM terrain dataset as a Dynamic Mesh. */
UCLASS()
class EONFORMRUNTIME_API AEonformTerrainDynamicMeshActor : public AActor
{
	GENERATED_BODY()

public:
	AEonformTerrainDynamicMeshActor();

	bool ApplyTerrainDataset(
		const FEonformTerrainDataset& Dataset,
		float HeightScale,
		FString* OutError = nullptr);

	bool ApplyTerrainDataset(
		const FEonformTerrainDataset& Dataset,
		const FEonformTerrainMeshBuildOptions& Options,
		FString* OutError = nullptr);

	UDynamicMeshComponent* GetTerrainMeshComponent() const
	{
		return TerrainMesh;
	}

private:
	UPROPERTY(VisibleAnywhere, Category="EONFORM")
	TObjectPtr<UDynamicMeshComponent> TerrainMesh;
};
