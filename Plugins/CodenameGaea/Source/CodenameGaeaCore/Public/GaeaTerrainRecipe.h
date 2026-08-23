#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainRecipe.generated.h"

namespace GaeaTerrainNodeTypes
{
	CODENAMEGAEACORE_API extern const FName SourceDataset;
	CODENAMEGAEACORE_API extern const FName PerlinNoise;
	CODENAMEGAEACORE_API extern const FName Cellular;
	CODENAMEGAEACORE_API extern const FName Cellular3D;
	CODENAMEGAEACORE_API extern const FName Cone;
	CODENAMEGAEACORE_API extern const FName Constant;
	CODENAMEGAEACORE_API extern const FName Cracks;
	CODENAMEGAEACORE_API extern const FName DotNoise;
	CODENAMEGAEACORE_API extern const FName Draw;
	CODENAMEGAEACORE_API extern const FName DriftNoise;
	CODENAMEGAEACORE_API extern const FName File;
	CODENAMEGAEACORE_API extern const FName Gabor;
	CODENAMEGAEACORE_API extern const FName Hemisphere;
	CODENAMEGAEACORE_API extern const FName LinearGradient;
	CODENAMEGAEACORE_API extern const FName LineNoise;
	CODENAMEGAEACORE_API extern const FName MultiFractal;
	CODENAMEGAEACORE_API extern const FName Noise;
	CODENAMEGAEACORE_API extern const FName Object;
	CODENAMEGAEACORE_API extern const FName Pattern;
	CODENAMEGAEACORE_API extern const FName RadialGradient;
	CODENAMEGAEACORE_API extern const FName Shape;
	CODENAMEGAEACORE_API extern const FName TileInput;
	CODENAMEGAEACORE_API extern const FName Voronoi;
	CODENAMEGAEACORE_API extern const FName WaveShine;
	CODENAMEGAEACORE_API extern const FName TerrainShape;
	CODENAMEGAEACORE_API extern const FName TerrainContext;
	CODENAMEGAEACORE_API extern const FName Geology;
	CODENAMEGAEACORE_API extern const FName ProcessMasks;
	CODENAMEGAEACORE_API extern const FName ThermalErosion;
	CODENAMEGAEACORE_API extern const FName HydraulicErosion;
	CODENAMEGAEACORE_API extern const FName HydroFix;
	CODENAMEGAEACORE_API extern const FName Rivers;
	CODENAMEGAEACORE_API extern const FName Lake;
	CODENAMEGAEACORE_API extern const FName Sediments;
	CODENAMEGAEACORE_API extern const FName Debris;
	CODENAMEGAEACORE_API extern const FName Scree;
	CODENAMEGAEACORE_API extern const FName EasyErosion;
	CODENAMEGAEACORE_API extern const FName Erosion2;
	CODENAMEGAEACORE_API extern const FName Thermal2;
	CODENAMEGAEACORE_API extern const FName Crumble;
	CODENAMEGAEACORE_API extern const FName Hillify;
	CODENAMEGAEACORE_API extern const FName Combine;
	CODENAMEGAEACORE_API extern const FName Clamp;
	CODENAMEGAEACORE_API extern const FName Adjust;
	CODENAMEGAEACORE_API extern const FName Aperture;
	CODENAMEGAEACORE_API extern const FName AutoLevel;
	CODENAMEGAEACORE_API extern const FName BlobRemover;
	CODENAMEGAEACORE_API extern const FName Blur;
	CODENAMEGAEACORE_API extern const FName Clip;
	CODENAMEGAEACORE_API extern const FName Curve;
	CODENAMEGAEACORE_API extern const FName Deflate;
	CODENAMEGAEACORE_API extern const FName Denoise;
	CODENAMEGAEACORE_API extern const FName Dilate;
	CODENAMEGAEACORE_API extern const FName DirectionalWarp;
	CODENAMEGAEACORE_API extern const FName Distance;
	CODENAMEGAEACORE_API extern const FName Equalize;
	CODENAMEGAEACORE_API extern const FName Extend;
	CODENAMEGAEACORE_API extern const FName Filter;
	CODENAMEGAEACORE_API extern const FName Flip;
	CODENAMEGAEACORE_API extern const FName Fold;
	CODENAMEGAEACORE_API extern const FName GraphicEQ;
	CODENAMEGAEACORE_API extern const FName Heal;
	CODENAMEGAEACORE_API extern const FName Match;
	CODENAMEGAEACORE_API extern const FName Median;
	CODENAMEGAEACORE_API extern const FName Meshify;
	CODENAMEGAEACORE_API extern const FName Origami;
	CODENAMEGAEACORE_API extern const FName Pixelate;
	CODENAMEGAEACORE_API extern const FName Recurve;
	CODENAMEGAEACORE_API extern const FName Shaper;
	CODENAMEGAEACORE_API extern const FName Sharpen;
	CODENAMEGAEACORE_API extern const FName SlopeBlur;
	CODENAMEGAEACORE_API extern const FName SlopeWarp;
	CODENAMEGAEACORE_API extern const FName SoftClip;
	CODENAMEGAEACORE_API extern const FName Swirl;
	CODENAMEGAEACORE_API extern const FName ThermalShaper;
	CODENAMEGAEACORE_API extern const FName Threshold;
	CODENAMEGAEACORE_API extern const FName Transform;
	CODENAMEGAEACORE_API extern const FName Transpose;
	CODENAMEGAEACORE_API extern const FName VariableBlur;
	CODENAMEGAEACORE_API extern const FName Warp;
	CODENAMEGAEACORE_API extern const FName Whorl;
	CODENAMEGAEACORE_API extern const FName Invert;
	CODENAMEGAEACORE_API extern const FName MultiCombine;
	CODENAMEGAEACORE_API extern const FName Sine;
	CODENAMEGAEACORE_API extern const FName ZeroBorders;
	CODENAMEGAEACORE_API extern const FName FractalTerraces;
	CODENAMEGAEACORE_API extern const FName Terrace;
	CODENAMEGAEACORE_API extern const FName Bomber;
	CODENAMEGAEACORE_API extern const FName Bulbous;
	CODENAMEGAEACORE_API extern const FName Contours;
	CODENAMEGAEACORE_API extern const FName Craggy;
	CODENAMEGAEACORE_API extern const FName Distress;
	CODENAMEGAEACORE_API extern const FName Grid;
	CODENAMEGAEACORE_API extern const FName GroundTexture;
	CODENAMEGAEACORE_API extern const FName Outcrops;
	CODENAMEGAEACORE_API extern const FName Pockmarks;
	CODENAMEGAEACORE_API extern const FName RockNoise;
	CODENAMEGAEACORE_API extern const FName Rockscape;
	CODENAMEGAEACORE_API extern const FName Roughen;
	CODENAMEGAEACORE_API extern const FName Sand;
	CODENAMEGAEACORE_API extern const FName Sandstone;
	CODENAMEGAEACORE_API extern const FName Shatter;
	CODENAMEGAEACORE_API extern const FName Shear;
	CODENAMEGAEACORE_API extern const FName Steps;
	CODENAMEGAEACORE_API extern const FName Stones;
	CODENAMEGAEACORE_API extern const FName Stratify;
	CODENAMEGAEACORE_API extern const FName Slope;
	CODENAMEGAEACORE_API extern const FName Angle;
	CODENAMEGAEACORE_API extern const FName Curvature;
	CODENAMEGAEACORE_API extern const FName Height;
}

USTRUCT(BlueprintType)
struct CODENAMEGAEACORE_API FGaeaTerrainNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Terrain Node") FGuid Id;
	UPROPERTY(EditAnywhere, Category="Terrain Node") FName Type = NAME_None;
	UPROPERTY(EditAnywhere, Category="Terrain Node") TMap<FName, double> NumericParameters;
	UPROPERTY(EditAnywhere, Category="Terrain Node") TMap<FName, int64> IntegerParameters;
	UPROPERTY(EditAnywhere, Category="Terrain Node") TMap<FName, bool> BoolParameters;
	UPROPERTY(EditAnywhere, Category="Terrain Node") TMap<FName, FName> NameParameters;
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
	UPROPERTY(EditAnywhere, Category="Terrain Connection") FGuid FromNode;
	UPROPERTY(EditAnywhere, Category="Terrain Connection") FName FromOutput = NAME_None;
	UPROPERTY(EditAnywhere, Category="Terrain Connection") FGuid ToNode;
	UPROPERTY(EditAnywhere, Category="Terrain Connection") FName ToInput = NAME_None;
	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct CODENAMEGAEACORE_API FGaeaTerrainRecipe
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Terrain Recipe") int32 Version = 1;
	UPROPERTY(EditAnywhere, Category="Terrain Recipe") FGuid OutputNode;
	UPROPERTY(EditAnywhere, Category="Terrain Recipe") TArray<FGaeaTerrainNode> Nodes;
	UPROPERTY(EditAnywhere, Category="Terrain Recipe") TArray<FGaeaTerrainConnection> Connections;
	const FGaeaTerrainNode* FindNode(const FGuid& NodeId) const;
	bool Validate(FString* OutError = nullptr) const;
	uint32 GetDeterministicHash() const;
};
