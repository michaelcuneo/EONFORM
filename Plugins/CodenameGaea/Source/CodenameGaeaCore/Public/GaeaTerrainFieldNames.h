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

	// EONFORM persistent hydrology state. FlowDirection stores a D8 receiver
	// index (0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE, -1=outlet).
	// FlowAccumulation stores upstream contributing sample count including self.
	CODENAMEGAEACORE_API extern const FName FlowDirection;
	CODENAMEGAEACORE_API extern const FName FlowAccumulation;
}
