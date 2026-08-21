#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainGraphAsset.generated.h"

UENUM(BlueprintType)
enum class EGaeaTerrainOutputSectionLayout : uint8
{
	Automatic,
	Explicit
};

UENUM(BlueprintType)
enum class EGaeaTerrainOutputComplexity : uint8
{
	Responsive,
	Balanced,
	Detailed,
	Maximum
};

/** Serializable physical/output settings that travel with an EONFORM graph asset. */
USTRUCT(BlueprintType)
struct CODENAMEGAEARUNTIME_API FGaeaTerrainGraphOutputSettings
{
	GENERATED_BODY()

	/** Final physical world width. EONFORM uses Unreal centimetres internally. */
	UPROPERTY(EditAnywhere, Category="Terrain Output", meta=(ClampMin="0.001", Units="km"))
	double WorldWidthKilometers = 1.0;

	/** Final physical world depth. */
	UPROPERTY(EditAnywhere, Category="Terrain Output", meta=(ClampMin="0.001", Units="km"))
	double WorldDepthKilometers = 1.0;

	/** Physical height represented by a normalized Height value of 1.0. Sea level remains world Z = 0. */
	UPROPERTY(EditAnywhere, Category="Terrain Output", meta=(ClampMin="0.001", Units="m"))
	double ElevationScaleMeters = 80.0;

	/** Zero means use the evaluated Height field's native resolution. */
	UPROPERTY(EditAnywhere, Category="Terrain Output", meta=(ClampMin="0"))
	int32 OutputResolution = 0;

	UPROPERTY(EditAnywhere, Category="Terrain Output")
	EGaeaTerrainOutputSectionLayout SectionLayout = EGaeaTerrainOutputSectionLayout::Automatic;

	UPROPERTY(EditAnywhere, Category="Terrain Output")
	EGaeaTerrainOutputComplexity SectionComplexity = EGaeaTerrainOutputComplexity::Balanced;

	UPROPERTY(EditAnywhere, Category="Terrain Output", meta=(ClampMin="1"))
	int32 SectionsX = 1;

	UPROPERTY(EditAnywhere, Category="Terrain Output", meta=(ClampMin="1"))
	int32 SectionsY = 1;

	/** Soft reference keeps the runtime graph asset independent of UE's experimental MeshPartition module. */
	UPROPERTY(EditAnywhere, Category="Terrain Output")
	FSoftObjectPath MeshPartitionDefinition;
};

USTRUCT()
struct CODENAMEGAEARUNTIME_API FGaeaTerrainNodeLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Graph Layout")
	FGuid NodeId;

	UPROPERTY(EditAnywhere, Category="Graph Layout")
	FVector2D Position = FVector2D::ZeroVector;
};

/** Saveable EONFORM graph asset containing the runtime recipe, editor layout, and physical output contract. */
UCLASS(BlueprintType)
class CODENAMEGAEARUNTIME_API UGaeaTerrainGraphAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Terrain Graph")
	FGaeaTerrainRecipe Recipe;

	UPROPERTY(EditAnywhere, Category="Terrain Graph")
	TArray<FGaeaTerrainNodeLayout> NodeLayout;

	UPROPERTY(EditAnywhere, Category="Terrain Output")
	FGaeaTerrainGraphOutputSettings OutputSettings;

	virtual void PostLoad() override;

	const FGaeaTerrainNodeLayout* FindLayout(const FGuid& NodeId) const;
	void SetLayout(const FGuid& NodeId, const FVector2D& Position);

private:
	void MigrateLegacyConnections();
};
