#pragma once

#include "CoreMinimal.h"

class ISlateStyle;

/** Shared EONFORM Slate resources used by the editor module. */
class FGaeaEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();

	static FName GetStyleSetName();
	static const ISlateStyle& Get();
};
