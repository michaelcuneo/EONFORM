#include "GaeaTerrainGraphPin.h"

#include "GaeaEditorGraph.h"
#include "Widgets/SNullWidget.h"

namespace
{
	bool IsEonformGraphPin(const UEdGraphPin* Pin)
	{
		if (!Pin) return false;
		const FName Category = Pin->PinType.PinCategory;
		return Category == GaeaEditorGraphPins::Terrain
			|| Category == GaeaEditorGraphPins::ScalarField
			|| Category == GaeaEditorGraphPins::Any;
	}
}

void SGaeaTerrainGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	EnableDragAndDrop(true);
}

TSharedRef<SWidget> SGaeaTerrainGraphPin::GetDefaultValueWidget()
{
	return SNullWidget::NullWidget;
}

TSharedPtr<SGraphPin> FGaeaTerrainGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (!IsEonformGraphPin(Pin))
	{
		return nullptr;
	}

	return SNew(SGaeaTerrainGraphPin, Pin);
}
