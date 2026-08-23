#pragma once

#include "CoreMinimal.h"

namespace GaeaTerrainFieldNames
{
	CODENAMEGAEACORE_API extern const FName Height;
	CODENAMEGAEACORE_API extern const FName Elevation;
	CODENAMEGAEACORE_API extern const FName SlopeDegrees;
	CODENAMEGAEACORE_API extern const FName Concavity;
	CODENAMEGAEACORE_API extern const FName Convexity;
	CODENAMEGAEACORE_API extern const FName Mountain;
	CODENAMEGAEACORE_API extern const FName Foothill;
	CODENAMEGAEACORE_API extern const FName Plains;
	CODENAMEGAEACORE_API extern const FName Thermal;
	CODENAMEGAEACORE_API extern const FName Rainfall;
	CODENAMEGAEACORE_API extern const FName HydraulicErosion;
	CODENAMEGAEACORE_API extern const FName Deposition;
	CODENAMEGAEACORE_API extern const FName Evaporation;
	CODENAMEGAEACORE_API extern const FName RockHardness;
	CODENAMEGAEACORE_API extern const FName Weathering;
	CODENAMEGAEACORE_API extern const FName SoilDepth;
	CODENAMEGAEACORE_API extern const FName Wear;
	CODENAMEGAEACORE_API extern const FName Deposits;
	CODENAMEGAEACORE_API extern const FName Flow;

	// Persistent D8 hydrology state.
	CODENAMEGAEACORE_API extern const FName FlowDirection;
	/** Legacy/sample-space accumulation: upstream contributing sample count including self. */
	CODENAMEGAEACORE_API extern const FName FlowAccumulation;
	/** Physical upstream contributing area in square kilometres. */
	CODENAMEGAEACORE_API extern const FName CatchmentAreaKm2;
	/** Physical downstream path length to the terrain-domain outlet in kilometres. */
	CODENAMEGAEACORE_API extern const FName DistanceToOutletKm;
	/** Strahler stream order derived from the D8 drainage tree. */
	CODENAMEGAEACORE_API extern const FName StreamOrder;

	// Public Derive flow-map semantics built from the persistent hydrology state.
	CODENAMEGAEACORE_API extern const FName FlowMap;
	CODENAMEGAEACORE_API extern const FName FlowMapDirection;
	CODENAMEGAEACORE_API extern const FName FlowMapAccumulation;
	CODENAMEGAEACORE_API extern const FName FlowMapClassic;
	CODENAMEGAEACORE_API extern const FName FlowMapClassicHierarchy;

	// Public terrain-semantic Derive fields.
	CODENAMEGAEACORE_API extern const FName Peaks;
	CODENAMEGAEACORE_API extern const FName RockMap;
	CODENAMEGAEACORE_API extern const FName Soil;
}
