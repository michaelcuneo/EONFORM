#pragma once

#include "CoreMinimal.h"

namespace GaeaTerrainNodeTypes
{
	CODENAMEGAEACORE_API extern const FName SourceDataset;
	CODENAMEGAEACORE_API extern const FName HydraulicErosion;
}

struct CODENAMEGAEACORE_API FGaeaTerrainNode
{
	FGuid Id;
	FName Type = NAME_None;
	TMap<FName, double> NumericParameters;
	TMap<FName, int64> IntegerParameters;
	TMap<FName, bool> BoolParameters;
	TMap<FName, FName> NameParameters;

	bool IsValid() const;
	double GetNumber(FName Name, double DefaultValue) const;
	int64 GetInteger(FName Name, int64 DefaultValue) const;
	bool GetBool(FName Name, bool DefaultValue) const;
	FName GetName(FName Name, FName DefaultValue = NAME_None) const;
};

struct CODENAMEGAEACORE_API FGaeaTerrainConnection
{
	FGuid FromNode;
	FName FromOutput = NAME_None;
	FGuid ToNode;
	FName ToInput = NAME_None;

	bool IsValid() const;
};

/** Runtime-safe, editor-independent terrain graph recipe. */
struct CODENAMEGAEACORE_API FGaeaTerrainRecipe
{
	int32 Version = 1;
	FGuid OutputNode;
	TArray<FGaeaTerrainNode> Nodes;
	TArray<FGaeaTerrainConnection> Connections;

	const FGaeaTerrainNode* FindNode(const FGuid& NodeId) const;
	bool Validate(FString* OutError = nullptr) const;
	uint32 GetDeterministicHash() const;
};
