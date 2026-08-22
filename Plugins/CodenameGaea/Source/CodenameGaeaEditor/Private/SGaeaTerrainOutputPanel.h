#pragma once

#include "CoreMinimal.h"
#include "GaeaMeshTerrainOutput.h"
#include "GaeaTerrainGraphAsset.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

template<typename OptionType>
class SComboBox;

class SGaeaTerrainOutputPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainOutputPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void InitializePresets();
	FReply GenerateTerrain();
	bool GenerateAvailableTerrain();
	FReply EditMeshPartitionDefinition();

	FIntPoint ResolveOutputResolution() const;
	FGaeaMeshTerrainOutputSettings MakeMeshTerrainSettings() const;
	bool ApplyPhysicalScale(const struct FGaeaTerrainDatasetSnapshot& Snapshot, FGaeaMeshTerrainOutputSettings& InOutSettings, FString& OutError) const;

	FText GetResolutionLabel(int32 Resolution) const;
	FText GetSectionLayoutLabel(EGaeaTerrainOutputSectionLayout Layout) const;
	FText GetSectionComplexityLabel(EGaeaTerrainOutputComplexity Complexity) const;
	FText GetOutputEstimateText() const;
	FText GetGenerateButtonText() const;

	TSharedPtr<int32> FindResolutionPreset(int32 Resolution) const;
	TSharedPtr<EGaeaTerrainOutputSectionLayout> FindSectionLayoutPreset(EGaeaTerrainOutputSectionLayout Layout) const;
	TSharedPtr<EGaeaTerrainOutputComplexity> FindSectionComplexityPreset(EGaeaTerrainOutputComplexity Complexity) const;

	TArray<TSharedPtr<int32>> ResolutionPresets;
	TArray<TSharedPtr<EGaeaTerrainOutputSectionLayout>> SectionLayoutPresets;
	TArray<TSharedPtr<EGaeaTerrainOutputComplexity>> SectionComplexityPresets;
	bool bGenerateQueued = false;
};
