#include "GaeaTerrainGraphAssetFactory.h"

#include "GaeaTerrainGraphAsset.h"
#include "GaeaTerrainOutputEditorState.h"

UGaeaTerrainGraphAssetFactory::UGaeaTerrainGraphAssetFactory()
{
	SupportedClass = UGaeaTerrainGraphAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = false;
}

UObject* UGaeaTerrainGraphAssetFactory::FactoryCreateNew(
	UClass* Class,
	UObject* InParent,
	FName Name,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	UGaeaTerrainGraphAsset* Asset = NewObject<UGaeaTerrainGraphAsset>(InParent, Class, Name, Flags | RF_Transactional);
	if (Asset)
	{
		// A graph can be configured before its first save. Seed newly-created assets
		// from the live Terrain Output state so those physical settings are not lost.
		Asset->OutputSettings = FGaeaTerrainOutputEditorState::Get().GetSettings();
	}
	return Asset;
}
