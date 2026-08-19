#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainGraphAsset.generated.h"

USTRUCT()
struct CODENAMEGAEARUNTIME_API FGaeaTerrainNodeLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Graph Layout")
	FGuid NodeId;

	UPROPERTY(EditAnywhere, Category="Graph Layout")
	FVector2D Position = FVector2D::ZeroVector;
};

/** Saveable Codename Gaea graph asset containing the runtime recipe plus editor layout. */
UCLASS(BlueprintType)
class CODENAMEGAEARUNTIME_API UGaeaTerrainGraphAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Terrain Graph")
	FGaeaTerrainRecipe Recipe;

	UPROPERTY(EditAnywhere, Category="Terrain Graph")
	TArray<FGaeaTerrainNodeLayout> NodeLayout;

	const FGaeaTerrainNodeLayout* FindLayout(const FGuid& NodeId) const;
	void SetLayout(const FGuid& NodeId, const FVector2D& Position);
};
