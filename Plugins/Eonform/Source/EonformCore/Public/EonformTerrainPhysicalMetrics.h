#pragma once

#include "CoreMinimal.h"

/**
 * Physical interpretation of a terrain graph evaluation.
 *
 * Terrain fields remain resolution/domain independent and normalized where appropriate,
 * while process solvers can resolve real sample spacing, catchment area and elevation.
 * With no active physical contract these values are zero and solvers use their authored
 * grid-domain fallback, preserving existing runtime callers and tests.
 */
struct EONFORMCORE_API FEonformTerrainPhysicalMetrics
{
	/** Snapshots the currently active physical contract, or zero metrics when none is active. */
	FEonformTerrainPhysicalMetrics();

	FEonformTerrainPhysicalMetrics(
		double InWorldWidthMeters,
		double InWorldDepthMeters,
		double InElevationScaleMeters,
		double InSeaLevelMeters)
		: WorldWidthMeters(InWorldWidthMeters)
		, WorldDepthMeters(InWorldDepthMeters)
		, ElevationScaleMeters(InElevationScaleMeters)
		, SeaLevelMeters(InSeaLevelMeters)
	{
	}

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

	/** Equal contributing area per hydrology sample; sums exactly to the configured world area. */
	double ResolveCellAreaSquareMeters(
		const FIntPoint& Resolution,
		const FVector2d& FallbackCellSizeCentimeters) const
	{
		if (HasWorldDimensions() && Resolution.X > 0 && Resolution.Y > 0)
		{
			return FMath::Max(
				(WorldWidthMeters * WorldDepthMeters)
					/ static_cast<double>(Resolution.X * Resolution.Y),
				UE_DOUBLE_SMALL_NUMBER);
		}

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

/** Optional active physical contract for editor-driven graph evaluation. */
class EONFORMCORE_API FEonformTerrainPhysicalContext
{
public:
	static void SetActive(const FEonformTerrainPhysicalMetrics& Metrics);
	static FEonformTerrainPhysicalMetrics GetActive();
	static uint64 GetRevision();
};
