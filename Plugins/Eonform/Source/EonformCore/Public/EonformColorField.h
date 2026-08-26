#pragma once

#include "CoreMinimal.h"
#include "EonformGridDomain.h"

/** Dense linear-color map sampled over the same grid domain used by heightfields. */
struct EONFORMCORE_API FEonformColorField
{
	FEonformGridDomain Domain;
	TArray<FLinearColor> Values;

	void Initialize(const FEonformGridDomain& InDomain, const FLinearColor& InitialValue = FLinearColor::Black)
	{
		Domain = InDomain;
		Values.Init(InitialValue, Domain.GetStorageSampleCount());
	}

	bool IsValid() const
	{
		return Domain.IsValid() && Values.Num() == Domain.GetStorageSampleCount();
	}

	FLinearColor& AtInterior(int32 X, int32 Y)
	{
		const int32 Border = Domain.BorderSamples;
		const FIntPoint Storage = Domain.GetStorageDimensions();
		return Values[(Y + Border) * Storage.X + (X + Border)];
	}

	const FLinearColor& AtInterior(int32 X, int32 Y) const
	{
		const int32 Border = Domain.BorderSamples;
		const FIntPoint Storage = Domain.GetStorageDimensions();
		return Values[(Y + Border) * Storage.X + (X + Border)];
	}
};
