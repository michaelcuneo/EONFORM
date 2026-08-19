#include "GaeaTerrainGraphAssetFactory.h"

#include "GaeaTerrainGraphAsset.h"

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
	return NewObject<UGaeaTerrainGraphAsset>(InParent, Class, Name, Flags | RF_Transactional);
}
