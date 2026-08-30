#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SUniformGridPanel;

class SEonformTerrainRegionGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEonformTerrainRegionGrid) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Rebuild();
	uint64 LastChangeSerial = 0;
	TSharedPtr<SUniformGridPanel> Grid;
};
