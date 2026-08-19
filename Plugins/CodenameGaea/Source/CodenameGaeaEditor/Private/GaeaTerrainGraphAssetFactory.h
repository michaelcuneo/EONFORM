#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "GaeaTerrainGraphAssetFactory.generated.h"

UCLASS()
class UGaeaTerrainGraphAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UGaeaTerrainGraphAssetFactory();

	virtual UObject* FactoryCreateNew(
		UClass* Class,
		UObject* InParent,
		FName Name,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;
};
