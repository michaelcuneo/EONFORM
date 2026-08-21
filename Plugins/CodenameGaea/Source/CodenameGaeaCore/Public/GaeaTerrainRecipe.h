#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainRecipe.generated.h"

namespace GaeaTerrainNodeTypes
{
	CODENAMEGAEACORE_API extern const FName SourceDataset;
	CODENAMEGAEACORE_API extern const FName PerlinNoise;
	CODENAMEGAEACORE_API extern const FName Cellular;
	CODENAMEGAEACORE_API extern const FName Cellular3D;
	CODENAMEGAEACORE_API extern const FName TerrainShape;
	CODENAMEGAEACORE_API extern const FName TerrainContext;
	CODENAMEGAEACORE_API extern const FName Geology;
	CODENAMEGAEACORE_API extern const FName ProcessMasks;
	CODENAMEGAEACORE_API extern const FName ThermalErosion;
	CODENAMEGAEACORE_API extern const FName HydraulicErosion;
	CODENAMEGAEACORE_API extern const FName Combine;
	CODENAMEGAEACORE_API extern const FName Clamp;
	CODENAMEGAEACORE_API extern const FName AutoLevel;
	CODENAMEGAEACORE_API extern const FName Blur;
	CODENAMEGAEACORE_API extern const FName Denoise;
	CODENAMEGAEACORE_API extern const FName Flip;
	CODENAMEGAEACORE_API extern const FName Invert;
	CODENAMEGAEACORE_API extern const FName MultiCombine;
	CODENAMEGAEACORE_API extern const FName Sharpen;
	CODENAMEGAEACORE_API extern const FName Sine;
	CODENAMEGAEACORE_API extern const FName Threshold;
	CODENAMEGAEACORE_API extern const FName Transform;
	CODENAMEGAEACORE_API extern const FName ZeroBorders;
	CODENAMEGAEACORE_API extern const FName Distance;
	CODENAMEGAEACORE_API extern const FName FractalTerraces;
	CODENAMEGAEACORE_API extern const FName Recurve;
	CODENAMEGAEACORE_API extern const FName Shaper;
	CODENAMEGAEACORE_API extern const FName SoftClip;
	CODENAMEGAEACORE_API extern const FName Terrace;
	CODENAMEGAEACORE_API extern const FName Slope;
	CODENAMEGAEACORE_API extern const FName Angle;
	CODENAMEGAEACORE_API extern const FName Curvature;
	CODENAMEGAEACORE_API extern const FName Height;
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
