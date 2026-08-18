#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerrainGeneratorActor.generated.h"

class UDynamicMeshComponent;

UCLASS()
class CODENAMEGAEA_API ATerrainGeneratorActor : public AActor
{
	GENERATED_BODY()

public:
	ATerrainGeneratorActor();

	UFUNCTION(CallInEditor, Category="Terrain")
	void Regenerate();

	UFUNCTION(CallInEditor, Category="Terrain")
	void RandomizeSeed();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category="Terrain")
	TObjectPtr<UDynamicMeshComponent> TerrainMesh;

	UPROPERTY(EditAnywhere, Category="Terrain|World", meta=(ClampMin="2", ClampMax="1025", UIMin="2", UIMax="513"))
	int32 Resolution = 257;

	UPROPERTY(EditAnywhere, Category="Terrain|World", meta=(ClampMin="100.0", UIMin="1000.0", UIMax="500000.0", Units="cm"))
	float WorldSize = 50000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|World", meta=(ClampMin="0.0", UIMin="100.0", UIMax="20000.0", Units="cm"))
	float HeightScale = 8000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="0.000001", ClampMax="1.0", UIMin="0.00005", UIMax="0.005"))
	float Frequency = 0.00055f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="1", ClampMax="12", UIMin="1", UIMax="10"))
	int32 Octaves = 6;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="1.0", ClampMax="4.0", UIMin="1.0", UIMax="3.0"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base")
	bool bCenterHeightfield = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp")
	bool bEnableDomainWarp = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp", meta=(EditCondition="bEnableDomainWarp", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00005", UIMax="0.003"))
	float WarpFrequency = 0.00035f;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp", meta=(EditCondition="bEnableDomainWarp", ClampMin="0.0", UIMin="0.0", UIMax="25000.0", Units="cm"))
	float WarpStrength = 9000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains")
	bool bEnableRidges = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00005", UIMax="0.005"))
	float RidgeFrequency = 0.0007f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="1", ClampMax="12", UIMin="1", UIMax="10"))
	int32 RidgeOctaves = 5;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float RidgeStrength = 0.7f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="0.25", ClampMax="8.0", UIMin="0.5", UIMax="4.0"))
	float RidgeSharpness = 1.6f;

	void BuildTerrain();
};
