#pragma once

#include "CoreMinimal.h"

/**
 * Physical interpretation of a terrain graph evaluation.
 *
 * Terrain fields remain resolution/domain independent and normalized where appropriate,
 * while process solvers can resolve real sample spacing, catchment area and elevation.
 * A zero/invalid dimension means "use the authored grid-domain fallback" so existing
 * runtime callers and tests preserve their historical behaviour.
 */
struct CODENAMEGAEACORE_API FGaeaTerrainPhysicalMetrics
{
	double WorldWidthMeters = 0.0;
	double WorldDepthMeters = 0.0;
	double ElevationScaleMeters = 0.0;
	double SeaLevelMeters = 0.0;

	bool HasWorldDimensions() const
	{
		return WorldWidthMeters > UE_DOUBLE_SMALL_NUMBER
			&& WorldDepthMeters > UE_DOUBLE_SMALL_NUMBER;
	}

	bool HasElevationScale() const
	{
		return ElevationScaleMeters > UE_DOUBLE_SMALL_NUMBER;
	}

	bool IsConfigured() const
	{
		return HasWorldDimensions() || HasElevationScale();
	}

	FVector2d ResolveSampleSpacingMeters(
		const FIntPoint& Resolution,
		const FVector2d& FallbackCellSizeCentimeters) const
	{
		if (HasWorldDimensions() && Resolution.X > 1 && Resolution.Y > 1)
		{
			return FVector2d(
				WorldWidthMeters / static_cast<double>(Resolution.X - 1),
				WorldDepthMeters / static_cast<double>(Resolution.Y - 1));
		}

		return FVector2d(
			FMath::Max(FMath::Abs(FallbackCellSizeCentimeters.X) * 0.01, UE_DOUBLE_SMALL_NUMBER),
			FMath::Max(FMath::Abs(FallbackCellSizeCentimeters.Y) * 0.01, UE_DOUBLE_SMALL_NUMBER));
	}

	double ResolveRepresentativeSampleSpacingMeters(
		const FIntPoint& Resolution,
		const FVector2d& FallbackCellSizeCentimeters) const
	{
		const FVector2d Spacing = ResolveSampleSpacingMeters(Resolution, FallbackCellSizeCentimeters);
		return FMath::Max(FMath::Min(Spacing.X, Spacing.Y), UE_DOUBLE_SMALL_NUMBER);
	}

	double ResolveCellAreaSquareMeters(
		const FIntPoint& Resolution,
		const FVector2d& FallbackCellSizeCentimeters) const
	{
		const FVector2d Spacing = ResolveSampleSpacingMeters(Resolution, FallbackCellSizeCentimeters);
		return FMath::Max(Spacing.X * Spacing.Y, UE_DOUBLE_SMALL_NUMBER);
	}

	double ResolveElevationScaleMeters(double FallbackHeightScaleCentimeters) const
	{
		return HasElevationScale()
			? ElevationScaleMeters
			: FMath::Max(FMath::Abs(FallbackHeightScaleCentimeters) * 0.01, UE_DOUBLE_SMALL_NUMBER);
	}

	double NormalizedHeightToMeters(double NormalizedHeight, double FallbackHeightScaleCentimeters) const
	{
		return SeaLevelMeters + NormalizedHeight * ResolveElevationScaleMeters(FallbackHeightScaleCentimeters);
	}

	double MetersToNormalizedHeight(double ElevationMeters, double FallbackHeightScaleCentimeters) const
	{
		return (ElevationMeters - SeaLevelMeters) / ResolveElevationScaleMeters(FallbackHeightScaleCentimeters);
	}

	double SamplesToMeters(
		double SampleCount,
		const FIntPoint& Resolution,
		const FVector2d& FallbackCellSizeCentimeters) const
	{
		return SampleCount * ResolveRepresentativeSampleSpacingMeters(Resolution, FallbackCellSizeCentimeters);
	}

	double CellsToSquareKilometers(
		double CellCount,
		const FIntPoint& Resolution,
		const FVector2d& FallbackCellSizeCentimeters) const
	{
		return CellCount * ResolveCellAreaSquareMeters(Resolution, FallbackCellSizeCentimeters) / 1000000.0;
	}
};

/**
 * Optional active physical contract for editor-driven graph evaluation.
 * Runtime callers do not need this: if nothing publishes an active contract,
 * GetActive() returns zero/default metrics and all solvers keep their legacy behaviour.
 */
class CODENAMEGAEACORE_API FGaeaTerrainPhysicalContext
{
public:
	static void SetActive(const FGaeaTerrainPhysicalMetrics& Metrics);
	static FGaeaTerrainPhysicalMetrics GetActive();
	static uint64 GetRevision();
};
