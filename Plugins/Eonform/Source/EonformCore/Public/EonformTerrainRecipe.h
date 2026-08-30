#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainRecipe.generated.h"

namespace EonformTerrainNodeTypes
{
	EONFORMCORE_API extern const FName SourceDataset;
	EONFORMCORE_API extern const FName PerlinNoise;
	EONFORMCORE_API extern const FName Cellular;
	EONFORMCORE_API extern const FName Cellular3D;
	EONFORMCORE_API extern const FName Cone;
	EONFORMCORE_API extern const FName Crater;
	EONFORMCORE_API extern const FName Constant;
	EONFORMCORE_API extern const FName Cracks;
	EONFORMCORE_API extern const FName DotNoise;
	EONFORMCORE_API extern const FName Draw;
	EONFORMCORE_API extern const FName DriftNoise;
	EONFORMCORE_API extern const FName File;
	EONFORMCORE_API extern const FName Gabor;
	EONFORMCORE_API extern const FName Hemisphere;
	EONFORMCORE_API extern const FName LinearGradient;
	EONFORMCORE_API extern const FName LineNoise;
	EONFORMCORE_API extern const FName MultiFractal;
	EONFORMCORE_API extern const FName Noise;
	EONFORMCORE_API extern const FName Object;
	EONFORMCORE_API extern const FName Pattern;
	EONFORMCORE_API extern const FName Plateau;
	EONFORMCORE_API extern const FName RadialGradient;
	EONFORMCORE_API extern const FName Shape;
	EONFORMCORE_API extern const FName TileInput;
	EONFORMCORE_API extern const FName Island;
	EONFORMCORE_API extern const FName Voronoi;
	EONFORMCORE_API extern const FName WaveShine;

	// EONFORM high-level Terrain landforms/composites.
	EONFORMCORE_API extern const FName Canyon;
	EONFORMCORE_API extern const FName Ridge;
	EONFORMCORE_API extern const FName Mountain;
	EONFORMCORE_API extern const FName Volcano;

	EONFORMCORE_API extern const FName TerrainShape;
	EONFORMCORE_API extern const FName TerrainContext;
	EONFORMCORE_API extern const FName Geology;
	EONFORMCORE_API extern const FName ProcessMasks;
	EONFORMCORE_API extern const FName ThermalErosion;
	EONFORMCORE_API extern const FName HydraulicErosion;
	EONFORMCORE_API extern const FName HydroFix;
	EONFORMCORE_API extern const FName Rivers;
	EONFORMCORE_API extern const FName Lake;
	EONFORMCORE_API extern const FName Sea;
	EONFORMCORE_API extern const FName Snow;
	EONFORMCORE_API extern const FName Snowfield;
	EONFORMCORE_API extern const FName Glacier;
	EONFORMCORE_API extern const FName Anastomosis;
	EONFORMCORE_API extern const FName Lichtenberg;
	EONFORMCORE_API extern const FName Trees;
	EONFORMCORE_API extern const FName Shrubs;
	EONFORMCORE_API extern const FName Wizard;
	EONFORMCORE_API extern const FName Wizard2;
	EONFORMCORE_API extern const FName Sediments;
	EONFORMCORE_API extern const FName Debris;
	EONFORMCORE_API extern const FName Scree;
	EONFORMCORE_API extern const FName EasyErosion;
	EONFORMCORE_API extern const FName Erosion2;
	EONFORMCORE_API extern const FName Thermal2;
	EONFORMCORE_API extern const FName Crumble;
	EONFORMCORE_API extern const FName Hillify;
	EONFORMCORE_API extern const FName Combine;
	EONFORMCORE_API extern const FName Clamp;
	EONFORMCORE_API extern const FName Adjust;
	EONFORMCORE_API extern const FName Aperture;
	EONFORMCORE_API extern const FName AutoLevel;
	EONFORMCORE_API extern const FName BlobRemover;
	EONFORMCORE_API extern const FName Blur;
	EONFORMCORE_API extern const FName Clip;
	EONFORMCORE_API extern const FName Curve;
	EONFORMCORE_API extern const FName Deflate;
	EONFORMCORE_API extern const FName Denoise;
	EONFORMCORE_API extern const FName Dilate;
	EONFORMCORE_API extern const FName DirectionalWarp;
	EONFORMCORE_API extern const FName Distance;
	EONFORMCORE_API extern const FName Equalize;
	EONFORMCORE_API extern const FName Extend;
	EONFORMCORE_API extern const FName Filter;
	EONFORMCORE_API extern const FName Flip;
	EONFORMCORE_API extern const FName Fold;
	EONFORMCORE_API extern const FName GraphicEQ;
	EONFORMCORE_API extern const FName Heal;
	EONFORMCORE_API extern const FName Match;
	EONFORMCORE_API extern const FName Median;
	EONFORMCORE_API extern const FName Meshify;
	EONFORMCORE_API extern const FName Origami;
	EONFORMCORE_API extern const FName Pixelate;
	EONFORMCORE_API extern const FName Recurve;
	EONFORMCORE_API extern const FName Shaper;
	EONFORMCORE_API extern const FName Sharpen;
	EONFORMCORE_API extern const FName SlopeBlur;
	EONFORMCORE_API extern const FName SlopeWarp;
	EONFORMCORE_API extern const FName SoftClip;
	EONFORMCORE_API extern const FName Swirl;
	EONFORMCORE_API extern const FName ThermalShaper;
	EONFORMCORE_API extern const FName Threshold;
	EONFORMCORE_API extern const FName Transform;
	EONFORMCORE_API extern const FName Transpose;
	EONFORMCORE_API extern const FName VariableBlur;
	EONFORMCORE_API extern const FName Warp;
	EONFORMCORE_API extern const FName Whorl;
	EONFORMCORE_API extern const FName Invert;
	EONFORMCORE_API extern const FName MultiCombine;
	EONFORMCORE_API extern const FName Sine;
	EONFORMCORE_API extern const FName ZeroBorders;
	EONFORMCORE_API extern const FName FractalTerraces;
	EONFORMCORE_API extern const FName Terrace;
	EONFORMCORE_API extern const FName Bomber;
	EONFORMCORE_API extern const FName Bulbous;
	EONFORMCORE_API extern const FName Contours;
	EONFORMCORE_API extern const FName Craggy;
	EONFORMCORE_API extern const FName Distress;
	EONFORMCORE_API extern const FName Grid;
	EONFORMCORE_API extern const FName GroundTexture;
	EONFORMCORE_API extern const FName Outcrops;
	EONFORMCORE_API extern const FName Pockmarks;
	EONFORMCORE_API extern const FName RockNoise;
	EONFORMCORE_API extern const FName Rockscape;
	EONFORMCORE_API extern const FName Roughen;
	EONFORMCORE_API extern const FName Sand;
	EONFORMCORE_API extern const FName Sandstone;
	EONFORMCORE_API extern const FName Shatter;
	EONFORMCORE_API extern const FName Shear;
	EONFORMCORE_API extern const FName Steps;
	EONFORMCORE_API extern const FName Stones;
	EONFORMCORE_API extern const FName Stratify;
	EONFORMCORE_API extern const FName Slope;
	EONFORMCORE_API extern const FName Angle;
	EONFORMCORE_API extern const FName Curvature;
	EONFORMCORE_API extern const FName Height;
	EONFORMCORE_API extern const FName FlowMap;
	EONFORMCORE_API extern const FName FlowMapClassic;
	EONFORMCORE_API extern const FName Peaks;
	EONFORMCORE_API extern const FName RockMap;
	EONFORMCORE_API extern const FName Soil;
	EONFORMCORE_API extern const FName Normals;
	EONFORMCORE_API extern const FName Occlusion;
	EONFORMCORE_API extern const FName TextureBase;
	EONFORMCORE_API extern const FName Texturizer;
	EONFORMCORE_API extern const FName ColorThreshold;

	// Public Utility nodes. These are thin graph wrappers over EonformTerrainUtilityOps.
	EONFORMCORE_API extern const FName Accumulator;
	EONFORMCORE_API extern const FName Chokepoint;
	EONFORMCORE_API extern const FName Compare;
	EONFORMCORE_API extern const FName DataExtractor;
	EONFORMCORE_API extern const FName Gate;
	EONFORMCORE_API extern const FName Layers;
	EONFORMCORE_API extern const FName Mask;
	EONFORMCORE_API extern const FName Math;
	EONFORMCORE_API extern const FName Mixer;
	EONFORMCORE_API extern const FName Repeat;
	EONFORMCORE_API extern const FName Reseed;
	EONFORMCORE_API extern const FName Route;
	EONFORMCORE_API extern const FName Seamless;
	EONFORMCORE_API extern const FName Switch;
	EONFORMCORE_API extern const FName Var;
}

USTRUCT(BlueprintType)
struct EONFORMCORE_API FEonformTerrainNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	FGuid Id;
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	FName Type = NAME_None;
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	TMap<FName, double> NumericParameters;
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	TMap<FName, int64> IntegerParameters;
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	TMap<FName, bool> BoolParameters;
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	TMap<FName, FName> NameParameters;
	UPROPERTY(EditAnywhere, Category = "Terrain Node")
	TMap<FName, FLinearColor> ColorParameters;
	bool IsValid() const;
	double GetNumber(FName Name, double DefaultValue) const;
	int64 GetInteger(FName Name, int64 DefaultValue) const;
	bool GetBool(FName Name, bool DefaultValue) const;
	FName GetName(FName Name, FName DefaultValue = NAME_None) const;
	FLinearColor GetColor(FName Name, const FLinearColor &DefaultValue = FLinearColor::White) const;
};

USTRUCT(BlueprintType)
struct EONFORMCORE_API FEonformTerrainConnection
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Terrain Connection")
	FGuid FromNode;
	UPROPERTY(EditAnywhere, Category = "Terrain Connection")
	FName FromOutput = NAME_None;
	UPROPERTY(EditAnywhere, Category = "Terrain Connection")
	FGuid ToNode;
	UPROPERTY(EditAnywhere, Category = "Terrain Connection")
	FName ToInput = NAME_None;
	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct EONFORMCORE_API FEonformTerrainRecipe
{
	GENERATED_BODY()

	FEonformTerrainRecipe()
	{
		// Small composite recipes are common in EONFORM. Reserving a modest block
		// keeps references to newly-added nodes stable while a composite is being
		// assembled, without changing the serialized graph representation.
		Nodes.Reserve(16);
		Connections.Reserve(16);
	}

	UPROPERTY(EditAnywhere, Category = "Terrain Recipe")
	int32 Version = 1;
	UPROPERTY(EditAnywhere, Category = "Terrain Recipe")
	FGuid OutputNode;
	UPROPERTY(EditAnywhere, Category = "Terrain Recipe")
	TArray<FEonformTerrainNode> Nodes;
	UPROPERTY(EditAnywhere, Category = "Terrain Recipe")
	TArray<FEonformTerrainConnection> Connections;
	const FEonformTerrainNode *FindNode(const FGuid &NodeId) const;
	bool Validate(FString *OutError = nullptr) const;
	uint32 GetDeterministicHash() const;
};