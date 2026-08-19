#include "GaeaTerrainGraphPin.h"

#include "GaeaEditorGraph.h"
#include "Widgets/SNullWidget.h"

namespace GaeaEditorGraphPins
{
	const FName Terrain(TEXT("GaeaTerrain"));
}

void SGaeaTerrainGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

TSharedRef<SWidget> SGaeaTerrainGraphPin::GetDefaultValueWidget()
{
	return SNullWidget::NullWidget;
}

TSharedPtr<SGraphPin> FGaeaTerrainGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (!Pin || Pin->PinType.PinCategory != GaeaEditorGraphPins::Terrain)
	{
		return nullptr;
	}

	return SNew(SGaeaTerrainGraphPin, Pin);
}
