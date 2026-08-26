#pragma once

#include "CoreMinimal.h"
#include "EonformMeshTerrainOutput.h"
#include "EonformTerrainGraphAsset.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

template<typename OptionType>
class SComboBox;

class SEonformTerrainOutputPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEonformTerrainOutputPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void InitializePresets();
	FReply GenerateTerrain();
	bool GenerateAvailableTerrain();
	FReply EditMeshPartitionDefinition();

	FIntPoint ResolveOutputResolution() const;
	FEonformMeshTerrainOutputSettings MakeMeshTerrainSettings() const;
	bool ApplyPhysicalScale(const struct FEonformTerrainDatasetSnapshot& Snapshot, FEonformMeshTerrainOutputSettings& InOutSettings, FString& OutError) const;

	FText GetResolutionLabel(int32 Resolution) const;
	FText GetSectionLayoutLabel(EEonformTerrainOutputSectionLayout Layout) const;
	FText GetSectionComplexityLabel(EEonformTerrainOutputComplexity Complexity) const;
	FText GetOutputEstimateText() const;
	FText GetGenerateButtonText() const;

	TSharedPtr<int32> FindResolutionPreset(int32 Resolution) const;
	TSharedPtr<EEonformTerrainOutputSectionLayout> FindSectionLayoutPreset(EEonformTerrainOutputSectionLayout Layout) const;
	TSharedPtr<EEonformTerrainOutputComplexity> FindSectionComplexityPreset(EEonformTerrainOutputComplexity Complexity) const;

	TArray<TSharedPtr<int32>> ResolutionPresets;
	TArray<TSharedPtr<EEonformTerrainOutputSectionLayout>> SectionLayoutPresets;
	TArray<TSharedPtr<EEonformTerrainOutputComplexity>> SectionComplexityPresets;
	bool bGenerateQueued = false;
};
