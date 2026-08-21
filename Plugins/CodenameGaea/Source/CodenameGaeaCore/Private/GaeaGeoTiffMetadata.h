#pragma once

#include "CoreMinimal.h"

struct FGaeaGeoTiffMetadata
{
	bool bValid = false;
	bool bGeographic = false;
	double XMin = 0.0;
	double YMin = 0.0;
	double XMax = 0.0;
	double YMax = 0.0;
	int32 EpsgCode = 0;
};

/** Reads the core GeoTIFF georeferencing tags from a classic TIFF file. */
bool GaeaReadGeoTiffMetadata(
	const FString& Path,
	int32 RasterWidth,
	int32 RasterHeight,
	FGaeaGeoTiffMetadata& OutMetadata);
