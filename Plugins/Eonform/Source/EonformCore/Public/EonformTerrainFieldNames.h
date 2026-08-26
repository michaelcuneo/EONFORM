#pragma once

#include "CoreMinimal.h"

namespace EonformTerrainFieldNames
{
	EONFORMCORE_API extern const FName Height;
	EONFORMCORE_API extern const FName Elevation;
	EONFORMCORE_API extern const FName SlopeDegrees;
	EONFORMCORE_API extern const FName Concavity;
	EONFORMCORE_API extern const FName Convexity;
	EONFORMCORE_API extern const FName Mountain;
	EONFORMCORE_API extern const FName Foothill;
	EONFORMCORE_API extern const FName Plains;

	// High-level Terrain landform semantics. These describe generated structure
	// and process readiness; they do not imply that hydrology has been solved.
	EONFORMCORE_API extern const FName MountainMass;
	EONFORMCORE_API extern const FName Uplift;
	EONFORMCORE_API extern const FName RidgeNetwork;
	EONFORMCORE_API extern const FName DrainageReadiness;
	EONFORMCORE_API extern const FName ErosionEligibility;
	EONFORMCORE_API extern const FName RockExposure;
	EONFORMCORE_API extern const FName CryosphereEligibility;

	EONFORMCORE_API extern const FName Thermal;
	EONFORMCORE_API extern const FName Rainfall;
	EONFORMCORE_API extern const FName HydraulicErosion;
	EONFORMCORE_API extern const FName Deposition;
	EONFORMCORE_API extern const FName Evaporation;
	EONFORMCORE_API extern const FName RockHardness;
	EONFORMCORE_API extern const FName Weathering;
	EONFORMCORE_API extern const FName SoilDepth;
	EONFORMCORE_API extern const FName Wear;
	EONFORMCORE_API extern const FName Deposits;
	EONFORMCORE_API extern const FName Flow;

	// Colorize handoff. SatMap publishes normalized RGB channels alongside
	// Terrain so runtime/editor mesh materialization can preserve graph color.
	EONFORMCORE_API extern const FName BaseColorR;
	EONFORMCORE_API extern const FName BaseColorG;
	EONFORMCORE_API extern const FName BaseColorB;

	// Persistent D8 hydrology state.
	EONFORMCORE_API extern const FName FlowDirection;
	/** Legacy/sample-space accumulation: upstream contributing sample count including self. */
	EONFORMCORE_API extern const FName FlowAccumulation;
	/** Physical upstream contributing area in square kilometres. */
	EONFORMCORE_API extern const FName CatchmentAreaKm2;
	/** Physical downstream path length to the terrain-domain outlet in kilometres. */
	EONFORMCORE_API extern const FName DistanceToOutletKm;
	/** Strahler stream order derived from the D8 drainage tree. */
	EONFORMCORE_API extern const FName StreamOrder;

	// Public Derive flow-map semantics built from the persistent hydrology state.
	EONFORMCORE_API extern const FName FlowMap;
	EONFORMCORE_API extern const FName FlowMapDirection;
	EONFORMCORE_API extern const FName FlowMapAccumulation;
	EONFORMCORE_API extern const FName FlowMapClassic;
	EONFORMCORE_API extern const FName FlowMapClassicHierarchy;

	// Public terrain-semantic Derive fields.
	EONFORMCORE_API extern const FName Peaks;
	EONFORMCORE_API extern const FName RockMap;
	EONFORMCORE_API extern const FName Soil;
	EONFORMCORE_API extern const FName TextureBase;
	EONFORMCORE_API extern const FName Texturizer;
	EONFORMCORE_API extern const FName ColorThreshold;

	// Surface-analysis fields. Normal channels are encoded from signed [-1, 1] to [0, 1].
	EONFORMCORE_API extern const FName NormalX;
	EONFORMCORE_API extern const FName NormalY;
	EONFORMCORE_API extern const FName NormalZ;
	EONFORMCORE_API extern const FName Occlusion;
}
