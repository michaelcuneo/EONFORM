#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GaeaTerrainDataset.h"
#include "GaeaTerrainMeshMaterializer.h"
#include "GaeaTerrainDynamicMeshActor.generated.h"

class UDynamicMeshComponent;

/** Runtime-safe scene actor that renders a Codename Gaea terrain dataset as a Dynamic Mesh. */
UCLASS()
class CODENAMEGAEARUNTIME_API AGaeaTerrainDynamicMeshActor : public AActor
{
	GENERATED_BODY()

public:
	AGaeaTerrainDynamicMeshActor();

	bool ApplyTerrainDataset(
		const FGaeaTerrainDataset& Dataset,
		float HeightScale,
		FString* OutError = nullptr);

	bool ApplyTerrainDataset(
		const FGaeaTerrainDataset& Dataset,
		const FGaeaTerrainMeshBuildOptions& Options,
		FString* OutError = nullptr);

	UDynamicMeshComponent* GetTerrainMeshComponent() const
	{
		return TerrainMesh;
	}

private:
	UPROPERTY(VisibleAnywhere, Category="Codename Gaea")
	TObjectPtr<UDynamicMeshComponent> TerrainMesh;
};
