#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SUniformGridPanel;

class SEonformTerrainRegionGrid : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEonformTerrainRegionGrid)
		: _SourceId(TEXT("EonformGraph"))
	{}
		SLATE_ARGUMENT(FName, SourceId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Refresh();
	uint32 BuildSnapshotHash() const;

	FName SourceId = NAME_None;
	TSharedPtr<SUniformGridPanel> GridPanel;
	uint32 LastSnapshotHash = 0;
	bool bHasSnapshotHash = false;
};
