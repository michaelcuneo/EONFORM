#include "GaeaEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace
{
	TSharedPtr<FSlateStyleSet> GaeaEditorStyleInstance;

	FSlateVectorImageBrush* MakeEonformVectorBrush(
		const TSharedRef<FSlateStyleSet>& Style,
		const TCHAR* RelativePath,
		const FVector2D& Size)
	{
		return new FSlateVectorImageBrush(
			Style->RootToContentDir(RelativePath, TEXT(".svg")),
			Size,
			FSlateColor::UseForeground());
	}
}

void FGaeaEditorStyle::Initialize()
{
	if (GaeaEditorStyleInstance.IsValid())
	{
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CodenameGaea"));
	if (!Plugin.IsValid())
	{
		return;
	}

	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
	Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	Style->Set(TEXT("EONFORM.Symbol.16"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(16.0f, 16.0f)));
	Style->Set(TEXT("EONFORM.Symbol.20"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(20.0f, 20.0f)));
	Style->Set(TEXT("EONFORM.Symbol.24"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(24.0f, 24.0f)));
	Style->Set(TEXT("EONFORM.Symbol.40"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(40.0f, 40.0f)));
	Style->Set(TEXT("EONFORM.Symbol.64"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(64.0f, 64.0f)));

	Style->Set(TEXT("EONFORM.Open"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(20.0f, 20.0f)));
	Style->Set(TEXT("EONFORM.Tab"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(16.0f, 16.0f)));

	// The graph asset uses the EONFORM mark in the Content Browser. Unreal discovers
	// these names automatically from the UObject class name.
	Style->Set(TEXT("ClassIcon.UGaeaTerrainGraphAsset"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(16.0f, 16.0f)));
	Style->Set(TEXT("ClassThumbnail.UGaeaTerrainGraphAsset"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(64.0f, 64.0f)));

	// Registered for branded editor surfaces. Navigation does not depend on the
	// wordmark SVGs because the symbol-only asset is the most robust Slate icon.
	Style->Set(TEXT("EONFORM.Brand.Symbol"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(64.0f, 30.0f)));
	Style->Set(TEXT("EONFORM.Brand.Horizontal"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.Horizontal.White"), FVector2D(240.0f, 60.0f)));
	Style->Set(TEXT("EONFORM.Brand.Stacked"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.Stacked.White"), FVector2D(160.0f, 120.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*Style);
	GaeaEditorStyleInstance = Style;
}

void FGaeaEditorStyle::Shutdown()
{
	if (!GaeaEditorStyleInstance.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*GaeaEditorStyleInstance);
	ensure(GaeaEditorStyleInstance.IsUnique());
	GaeaEditorStyleInstance.Reset();
}

FName FGaeaEditorStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("EONFORMEditorStyle"));
	return StyleSetName;
}

const ISlateStyle& FGaeaEditorStyle::Get()
{
	check(GaeaEditorStyleInstance.IsValid());
	return *GaeaEditorStyleInstance;
}
