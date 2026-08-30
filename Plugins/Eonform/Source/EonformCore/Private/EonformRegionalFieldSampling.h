#pragma once

#include "CoreMinimal.h"
#include "EonformScalarField.h"
#include "EonformTerrainEvaluator.h"

/**
 * Shared coordinate handling for bounded neighbourhood nodes during regional
 * evaluation. Internal region edges read the scheduler-provided storage halo;
 * samples beyond the actual full-world boundary clamp back onto that boundary,
 * matching the legacy full-raster edge condition.
 */
namespace EonformRegionalFieldSampling
{
	inline bool ResolveWorldDomain(
		const FEonformScalarField& Field,
		const FEonformTerrainEvaluationContext& Context,
		FEonformGridDomain& OutWorldDomain,
		FString& OutError)
	{
		if (!Field.IsValid())
		{
			OutError = TEXT("Regional field sampling requires a valid scalar field.");
			return false;
		}

		if (!Context.HasRegion())
		{
			OutWorldDomain = FEonformGridDomain::Make(
				Field.Domain.Dimensions,
				Field.Domain.WorldMin,
				Field.Domain.WorldMax);
			return OutWorldDomain.IsValid();
		}

		if (Context.ReferenceResolution.X < 2 || Context.ReferenceResolution.Y < 2)
		{
			OutError = TEXT("Regional neighbourhood evaluation requires a valid full-world reference resolution.");
			return false;
		}

		OutWorldDomain = Context.ResolveReferenceDomain(
			Context.ReferenceResolution,
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
		if (!OutWorldDomain.IsValid())
		{
			OutError = TEXT("Regional neighbourhood evaluation could not resolve the full-world reference domain.");
			return false;
		}
		return true;
	}

	inline FIntPoint ResolveStorageCoordinate(
		const FEonformScalarField& Field,
		int32 StorageX,
		int32 StorageY,
		const FEonformGridDomain& WorldDomain)
	{
		const FIntPoint StorageDimensions = Field.Domain.GetStorageDimensions();
		StorageX = FMath::Clamp(StorageX, 0, StorageDimensions.X - 1);
		StorageY = FMath::Clamp(StorageY, 0, StorageDimensions.Y - 1);

		FVector2d World = Field.Domain.StorageSampleToWorld(StorageX, StorageY);
		World.X = FMath::Clamp(World.X, WorldDomain.WorldMin.X, WorldDomain.WorldMax.X);
		World.Y = FMath::Clamp(World.Y, WorldDomain.WorldMin.Y, WorldDomain.WorldMax.Y);

		const FVector2d Coordinate = Field.Domain.WorldToStorageCoordinate(World);
		return FIntPoint(
			FMath::Clamp(FMath::RoundToInt(Coordinate.X), 0, StorageDimensions.X - 1),
			FMath::Clamp(FMath::RoundToInt(Coordinate.Y), 0, StorageDimensions.Y - 1));
	}

	inline float Sample(
		const FEonformScalarField& Field,
		int32 StorageX,
		int32 StorageY,
		const FEonformGridDomain& WorldDomain)
	{
		const FIntPoint Coordinate = ResolveStorageCoordinate(Field, StorageX, StorageY, WorldDomain);
		return Field.AtStorage(Coordinate.X, Coordinate.Y);
	}

	inline float SampleOffset(
		const FEonformScalarField& Field,
		const FIntPoint& Center,
		int32 OffsetX,
		int32 OffsetY,
		const FEonformGridDomain& WorldDomain)
	{
		return Sample(Field, Center.X + OffsetX, Center.Y + OffsetY, WorldDomain);
	}
}
