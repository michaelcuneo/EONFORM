#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EonformTerrainGraphAssetFactory.generated.h"

UCLASS()
class UEonformTerrainGraphAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UEonformTerrainGraphAssetFactory();

	virtual UObject* FactoryCreateNew(
		UClass* Class,
		UObject* InParent,
		FName Name,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;
};
