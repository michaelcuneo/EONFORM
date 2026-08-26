#include "EonformTerrainGraphAssetFactory.h"

#include "EonformTerrainGraphAsset.h"
#include "EonformTerrainOutputEditorState.h"

UEonformTerrainGraphAssetFactory::UEonformTerrainGraphAssetFactory()
{
	SupportedClass = UEonformTerrainGraphAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = false;
}

UObject* UEonformTerrainGraphAssetFactory::FactoryCreateNew(
	UClass* Class,
	UObject* InParent,
	FName Name,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	UEonformTerrainGraphAsset* Asset = NewObject<UEonformTerrainGraphAsset>(InParent, Class, Name, Flags | RF_Transactional);
	if (Asset)
	{
		// A graph can be configured before its first save. Seed newly-created assets
		// from the live Terrain Output state so those physical settings are not lost.
		Asset->OutputSettings = FEonformTerrainOutputEditorState::Get().GetSettings();
	}
	return Asset;
}
