#include "EonformTerrainGraphPin.h"

#include "EonformEditorGraph.h"
#include "Widgets/SNullWidget.h"

namespace
{
	bool IsEonformGraphPin(const UEdGraphPin* Pin)
	{
		if (!Pin) return false;
		const FName Category = Pin->PinType.PinCategory;
		return Category == EonformEditorGraphPins::Terrain
			|| Category == EonformEditorGraphPins::ScalarField
			|| Category == EonformEditorGraphPins::Any;
	}
}

void SEonformTerrainGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	EnableDragAndDrop(true);
}

TSharedRef<SWidget> SEonformTerrainGraphPin::GetDefaultValueWidget()
{
	return SNullWidget::NullWidget;
}

TSharedPtr<SGraphPin> FEonformTerrainGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (!IsEonformGraphPin(Pin))
	{
		return nullptr;
	}

	return SNew(SEonformTerrainGraphPin, Pin);
}
