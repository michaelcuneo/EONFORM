#include "GaeaEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
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
			FSlateColor::UseForeground(),
			ESlateBrushTileType::NoTile);
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
	Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));

	Style->Set(TEXT("EONFORM.Symbol.16"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(16.0f, 16.0f)));
	Style->Set(TEXT("EONFORM.Symbol.20"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(20.0f, 20.0f)));
	Style->Set(TEXT("EONFORM.Symbol.24"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(24.0f, 24.0f)));
	Style->Set(TEXT("EONFORM.Symbol.40"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(40.0f, 40.0f)));
	Style->Set(TEXT("EONFORM.Symbol.64"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(64.0f, 64.0f)));

	Style->Set(TEXT("EONFORM.Open"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(20.0f, 20.0f)));
	Style->Set(TEXT("EONFORM.Tab"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(16.0f, 16.0f)));

	// Unreal's reflected class name omits the native U-prefix, so these are the
	// canonical Content Browser lookup keys for UGaeaTerrainGraphAsset.
	Style->Set(TEXT("ClassIcon.GaeaTerrainGraphAsset"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(16.0f, 16.0f)));
	Style->Set(TEXT("ClassThumbnail.GaeaTerrainGraphAsset"), MakeEonformVectorBrush(Style, TEXT("Icons/Eonform.White"), FVector2D(64.0f, 64.0f)));

	// Keep prefixed aliases too; they are useful to explicit callers and harmless
	// if Unreal never asks for them automatically.
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
