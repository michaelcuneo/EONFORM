#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainRecipe.generated.h"

namespace GaeaTerrainNodeTypes
{
	CODENAMEGAEACORE_API extern const FName SourceDataset;
	CODENAMEGAEACORE_API extern const FName ProceduralTerrain;
	CODENAMEGAEACORE_API extern const FName TerrainShape;
	CODENAMEGAEACORE_API extern const FName TerrainContext;
	CODENAMEGAEACORE_API extern const FName Geology;
	CODENAMEGAEACORE_API extern const FName ProcessMasks;
	CODENAMEGAEACORE_API extern const FName HydraulicErosion;
}

USTRUCT(BlueprintType)
struct CODENAMEGAEACORE_API FGaeaTerrainNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Terrain Node")
	FGuid Id;

	UPROPERTY(EditAnywhere, Category="Terrain Node")
	FName Type = NAME_None;

	UPROPERTY(EditAnywhere, Category="Terrain Node")
	TMap<FName, double> NumericParameters;

	UPROPERTY(EditAnywhere, Category="Terrain Node")
	TMap<FName, int64> IntegerParameters;

	UPROPERTY(EditAnywhere, Category="Terrain Node")
	TMap<FName, bool> BoolParameters;

	UPROPERTY(EditAnywhere, Category="Terrain Node")
	TMap<FName, FName> NameParameters;

	bool IsValid() const;
	double GetNumber(FName Name, double DefaultValue) const;
	int64 GetInteger(FName Name, int64 DefaultValue) const;
	bool GetBool(FName Name, bool DefaultValue) const;
	FName GetName(FName Name, FName DefaultValue = NAME_None) const;
};

USTRUCT(BlueprintType)
struct CODENAMEGAEACORE_API FGaeaTerrainConnection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Terrain Connection")
	FGuid FromNode;

	UPROPERTY(EditAnywhere, Category="Terrain Connection")
	FName FromOutput = NAME_None;

	UPROPERTY(EditAnywhere, Category="Terrain Connection")
	FGuid ToNode;

	UPROPERTY(EditAnywhere, Category="Terrain Connection")
	FName ToInput = NAME_None;

	bool IsValid() const;
};

/** Runtime-safe, editor-independent terrain graph recipe. */
USTRUCT(BlueprintType)
struct CODENAMEGAEACORE_API FGaeaTerrainRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Terrain Recipe")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, Category="Terrain Recipe")
	FGuid OutputNode;

	UPROPERTY(EditAnywhere, Category="Terrain Recipe")
	TArray<FGaeaTerrainNode> Nodes;

	UPROPERTY(EditAnywhere, Category="Terrain Recipe")
	TArray<FGaeaTerrainConnection> Connections;

	const FGaeaTerrainNode* FindNode(const FGuid& NodeId) const;
	bool Validate(FString* OutError = nullptr) const;
	uint32 GetDeterministicHash() const;
};
