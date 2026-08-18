#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerrainGeneratorActor.generated.h"

class UDynamicMeshComponent;
struct FTerrainHeightField;

UCLASS()
class CODENAMEGAEA_API ATerrainGeneratorActor : public AActor
{
	GENERATED_BODY()

public:
	ATerrainGeneratorActor();

	UFUNCTION(CallInEditor, Category="Terrain")
	void Regenerate();

	UFUNCTION(CallInEditor, Category="Terrain")
	void RandomizeSeed();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category="Terrain")
	TObjectPtr<UDynamicMeshComponent> TerrainMesh;

	UPROPERTY(VisibleAnywhere, Category="Terrain")
	TObjectPtr<UDynamicMeshComponent> RiverWaterMesh;

	UPROPERTY(EditAnywhere, Category="Terrain|World", meta=(ClampMin="2", ClampMax="1025", UIMin="2", UIMax="513"))
	int32 Resolution = 257;

	UPROPERTY(EditAnywhere, Category="Terrain|World", meta=(ClampMin="100.0", UIMin="1000.0", UIMax="500000.0", Units="cm"))
	float WorldSize = 50000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|World", meta=(ClampMin="0.0", UIMin="100.0", UIMax="20000.0", Units="cm"))
	float HeightScale = 8000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Landmass")
	bool bIsland = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Landmass")
	bool bArchipelago = false;

	UPROPERTY(EditAnywhere, Category="Terrain|Landmass", meta=(ClampMin="1000.0", ClampMax="200000.0", UIMin="5000.0", UIMax="80000.0", Units="cm"))
	float CoastScale = 32000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Landmass", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float CoastIrregularity = 0.62f;

	UPROPERTY(EditAnywhere, Category="Terrain|Landmass", meta=(ClampMin="0.05", ClampMax="0.95", UIMin="0.15", UIMax="0.85"))
	float LandCoverage = 0.5f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="100.0", ClampMax="50000.0", UIMin="500.0", UIMax="15000.0", Units="cm"))
	float ShelfWidth = 4500.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="0.0", ClampMax="5000.0", UIMin="100.0", UIMax="2000.0", Units="cm"))
	float ShelfDepth = 700.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="100.0", ClampMax="80000.0", UIMin="1000.0", UIMax="20000.0", Units="cm"))
	float ContinentalSlopeWidth = 6500.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="100.0", ClampMax="20000.0", UIMin="1000.0", UIMax="10000.0", Units="cm"))
	float BasinDepth = 5200.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="0.0", ClampMax="5000.0", UIMin="0.0", UIMax="2000.0", Units="cm"))
	float BasinRelief = 900.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="0.0", ClampMax="10000.0", UIMin="0.0", UIMax="4000.0", Units="cm"))
	float TrenchDepth = 1800.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="500.0", ClampMax="50000.0", UIMin="1000.0", UIMax="15000.0", Units="cm"))
	float SeamountScale = 7000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Bathymetry", meta=(ClampMin="0.0", ClampMax="10000.0", UIMin="0.0", UIMax="3000.0", Units="cm"))
	float SeamountHeight = 1400.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="0.000001", ClampMax="1.0", UIMin="0.00002", UIMax="0.002"))
	float Frequency = 0.00022f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="1", ClampMax="12", UIMin="1", UIMax="10"))
	int32 Octaves = 5;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float Persistence = 0.48f;

	UPROPERTY(EditAnywhere, Category="Terrain|Base", meta=(ClampMin="1.0", ClampMax="4.0", UIMin="1.0", UIMax="3.0"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Macro")
	bool bEnableMacroShape = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Macro", meta=(EditCondition="bEnableMacroShape", ClampMin="0.000001", ClampMax="1.0", UIMin="0.000005", UIMax="0.0005"))
	float MacroFrequency = 0.000055f;

	UPROPERTY(EditAnywhere, Category="Terrain|Macro", meta=(EditCondition="bEnableMacroShape", ClampMin="1", ClampMax="8", UIMin="1", UIMax="6"))
	int32 MacroOctaves = 3;

	UPROPERTY(EditAnywhere, Category="Terrain|Macro", meta=(EditCondition="bEnableMacroShape", ClampMin="0.0", ClampMax="2.0", UIMin="0.0", UIMax="1.5"))
	float MacroStrength = 0.75f;

	UPROPERTY(EditAnywhere, Category="Terrain|Macro", meta=(EditCondition="bEnableMacroShape", ClampMin="0.25", ClampMax="4.0", UIMin="0.5", UIMax="2.5"))
	float MacroContrast = 1.1f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains")
	bool bEnableMountainMask = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableMountainMask", ClampMin="-1.0", ClampMax="1.0", UIMin="-0.5", UIMax="0.75"))
	float MountainThreshold = 0.12f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableMountainMask", ClampMin="0.01", ClampMax="2.0", UIMin="0.05", UIMax="1.0"))
	float MountainTransition = 0.55f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains")
	bool bEnableRidges = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00002", UIMax="0.002"))
	float RidgeFrequency = 0.00032f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="1", ClampMax="12", UIMin="1", UIMax="10"))
	int32 RidgeOctaves = 4;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="0.0", ClampMax="2.0", UIMin="0.0", UIMax="1.5"))
	float RidgeStrength = 0.55f;

	UPROPERTY(EditAnywhere, Category="Terrain|Mountains", meta=(EditCondition="bEnableRidges", ClampMin="0.25", ClampMax="8.0", UIMin="0.5", UIMax="4.0"))
	float RidgeSharpness = 1.8f;

	UPROPERTY(EditAnywhere, Category="Terrain|Foothills")
	bool bEnableFoothills = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Foothills", meta=(EditCondition="bEnableFoothills", ClampMin="0.01", ClampMax="1.0", UIMin="0.05", UIMax="0.8"))
	float FoothillWidth = 0.55f;

	UPROPERTY(EditAnywhere, Category="Terrain|Foothills", meta=(EditCondition="bEnableFoothills", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.8"))
	float FoothillStrength = 0.28f;

	UPROPERTY(EditAnywhere, Category="Terrain|Foothills", meta=(EditCondition="bEnableFoothills", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00002", UIMax="0.001"))
	float FoothillFrequency = 0.00018f;

	UPROPERTY(EditAnywhere, Category="Terrain|Valleys")
	bool bEnableValleys = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Valleys", meta=(EditCondition="bEnableValleys", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00001", UIMax="0.0005"))
	float ValleyFrequency = 0.000085f;

	UPROPERTY(EditAnywhere, Category="Terrain|Valleys", meta=(EditCondition="bEnableValleys", ClampMin="0.01", ClampMax="1.0", UIMin="0.05", UIMax="0.75"))
	float ValleyWidth = 0.22f;

	UPROPERTY(EditAnywhere, Category="Terrain|Valleys", meta=(EditCondition="bEnableValleys", ClampMin="0.25", ClampMax="8.0", UIMin="0.5", UIMax="4.0"))
	float ValleySharpness = 1.4f;

	UPROPERTY(EditAnywhere, Category="Terrain|Valleys", meta=(EditCondition="bEnableValleys", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.6"))
	float ValleyDepth = 0.16f;

	UPROPERTY(EditAnywhere, Category="Terrain|Plains")
	bool bEnablePlains = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Plains", meta=(EditCondition="bEnablePlains", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float PlainsStrength = 0.55f;

	UPROPERTY(EditAnywhere, Category="Terrain|Plains", meta=(EditCondition="bEnablePlains", ClampMin="0.25", ClampMax="4.0", UIMin="0.5", UIMax="3.0"))
	float PlainsFlattenExponent = 1.65f;

	UPROPERTY(EditAnywhere, Category="Terrain|Plains", meta=(EditCondition="bEnablePlains", ClampMin="0.0", ClampMax="0.5", UIMin="0.0", UIMax="0.25"))
	float PlainsRollingStrength = 0.08f;

	UPROPERTY(EditAnywhere, Category="Terrain|Plains", meta=(EditCondition="bEnablePlains", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00001", UIMax="0.001"))
	float PlainsRollingFrequency = 0.00012f;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp")
	bool bEnableDomainWarp = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp", meta=(EditCondition="bEnableDomainWarp", ClampMin="0.000001", ClampMax="1.0", UIMin="0.00002", UIMax="0.001"))
	float WarpFrequency = 0.00012f;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp", meta=(EditCondition="bEnableDomainWarp", ClampMin="0.0", UIMin="0.0", UIMax="25000.0", Units="cm"))
	float WarpStrength = 4500.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Warp", meta=(EditCondition="bEnableDomainWarp", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float WarpRegionality = 0.85f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure")
	bool bEnableStructuralGeology = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure", meta=(EditCondition="bEnableStructuralGeology", ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="deg"))
	float StructureDirection = 32.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure", meta=(EditCondition="bEnableStructuralGeology", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.6"))
	float StructureCurvature = 0.28f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure", meta=(EditCondition="bEnableStructuralGeology", ClampMin="0.05", ClampMax="1.0", UIMin="0.2", UIMax="1.0"))
	float TectonicCoverage = 0.68f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Uplift", meta=(EditCondition="bEnableStructuralGeology", ClampMin="1000.0", ClampMax="100000.0", UIMin="5000.0", UIMax="40000.0", Units="cm"))
	float UpliftSpacing = 18000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Uplift", meta=(EditCondition="bEnableStructuralGeology", ClampMin="500.0", ClampMax="50000.0", UIMin="2000.0", UIMax="20000.0", Units="cm"))
	float UpliftWidth = 8500.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Uplift", meta=(EditCondition="bEnableStructuralGeology", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.5"))
	float UpliftStrength = 0.18f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Long Valleys", meta=(EditCondition="bEnableStructuralGeology", ClampMin="1000.0", ClampMax="100000.0", UIMin="5000.0", UIMax="40000.0", Units="cm"))
	float StructuralValleySpacing = 18000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Long Valleys", meta=(EditCondition="bEnableStructuralGeology", ClampMin="100.0", ClampMax="20000.0", UIMin="500.0", UIMax="6000.0", Units="cm"))
	float StructuralValleyWidth = 1800.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Long Valleys", meta=(EditCondition="bEnableStructuralGeology", ClampMin="0.0", ClampMax="2000.0", UIMin="0.0", UIMax="800.0", Units="cm"))
	float StructuralValleyDepth = 240.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Faults", meta=(EditCondition="bEnableStructuralGeology", ClampMin="1000.0", ClampMax="100000.0", UIMin="3000.0", UIMax="30000.0", Units="cm"))
	float FaultSpacing = 11000.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Faults", meta=(EditCondition="bEnableStructuralGeology", ClampMin="50.0", ClampMax="10000.0", UIMin="100.0", UIMax="2500.0", Units="cm"))
	float FaultWidth = 500.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Faults", meta=(EditCondition="bEnableStructuralGeology", ClampMin="-90.0", ClampMax="90.0", UIMin="-45.0", UIMax="45.0", Units="deg"))
	float FaultAngleOffset = 22.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Structure|Faults", meta=(EditCondition="bEnableStructuralGeology", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float FaultWeakness = 0.7f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate")
	bool bEnableClimate = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="0.0", ClampMax="360.0", UIMin="0.0", UIMax="360.0", Units="deg"))
	float PrevailingWindDirection = 235.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="-20.0", ClampMax="45.0", UIMin="-5.0", UIMax="35.0", Units="Celsius"))
	float BaseTemperatureC = 18.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="0.0", ClampMax="12.0", UIMin="3.0", UIMax="9.0"))
	float TemperatureLapseRate = 6.5f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float BaseHumidity = 0.62f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="0.0", ClampMax="2.0", UIMin="0.0", UIMax="1.5"))
	float OrographicStrength = 0.78f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="0.0", ClampMax="2.0", UIMin="0.0", UIMax="1.5"))
	float RainShadowStrength = 0.68f;

	UPROPERTY(EditAnywhere, Category="Terrain|Climate", meta=(EditCondition="bEnableClimate", ClampMin="0.0", ClampMax="0.5", UIMin="0.0", UIMax="0.2"))
	float MoistureRecovery = 0.08f;

	UPROPERTY(EditAnywhere, Category="Terrain|Biomes")
	bool bEnableBiomes = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Biomes", meta=(EditCondition="bEnableBiomes", ClampMin="0.0", ClampMax="1.0", UIMin="0.2", UIMax="0.8"))
	float ForestMoistureThreshold = 0.48f;

	UPROPERTY(EditAnywhere, Category="Terrain|Biomes", meta=(EditCondition="bEnableBiomes", ClampMin="5.0", ClampMax="70.0", UIMin="15.0", UIMax="45.0", Units="deg"))
	float ForestMaxSlope = 32.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Biomes", meta=(EditCondition="bEnableBiomes", ClampMin="0.0", ClampMax="1.0", UIMin="0.05", UIMax="0.6"))
	float AridMoistureThreshold = 0.28f;

	UPROPERTY(EditAnywhere, Category="Terrain|Biomes", meta=(EditCondition="bEnableBiomes", ClampMin="0.0", ClampMax="1.0", UIMin="0.4", UIMax="0.9"))
	float AlpineElevationThreshold = 0.68f;

	UPROPERTY(EditAnywhere, Category="Terrain|Biomes", meta=(EditCondition="bEnableBiomes", ClampMin="0.0", ClampMax="1.0", UIMin="0.2", UIMax="0.9"))
	float WetlandWetnessThreshold = 0.58f;

	UPROPERTY(EditAnywhere, Category="Terrain|Natural Processes")
	bool bUseNaturalProcessMasks = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Natural Processes", meta=(EditCondition="bUseNaturalProcessMasks", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float ThermalRegionality = 0.9f;

	UPROPERTY(EditAnywhere, Category="Terrain|Natural Processes", meta=(EditCondition="bUseNaturalProcessMasks", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float HydraulicRegionality = 0.85f;

	UPROPERTY(EditAnywhere, Category="Terrain|Natural Processes", meta=(EditCondition="bUseNaturalProcessMasks", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float RainfallHighlandBias = 0.65f;

	UPROPERTY(EditAnywhere, Category="Terrain|Natural Processes", meta=(EditCondition="bUseNaturalProcessMasks", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float EvaporationLowlandBias = 0.55f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Thermal")
	bool bEnableThermalErosion = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Thermal", meta=(EditCondition="bEnableThermalErosion", ClampMin="0", ClampMax="100", UIMin="0", UIMax="40"))
	int32 ThermalIterations = 12;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Thermal", meta=(EditCondition="bEnableThermalErosion", ClampMin="5.0", ClampMax="75.0", UIMin="20.0", UIMax="50.0", Units="deg"))
	float ThermalTalusAngle = 34.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Thermal", meta=(EditCondition="bEnableThermalErosion", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float ThermalStrength = 0.35f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic")
	bool bEnableHydraulicErosion = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0", ClampMax="200", UIMin="0", UIMax="80"))
	int32 HydraulicIterations = 24;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="0.1", UIMin="0.0", UIMax="0.04"))
	float HydraulicRainfall = 0.01f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float HydraulicFlowRate = 0.55f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="4.0", UIMin="0.0", UIMax="2.0"))
	float HydraulicSedimentCapacity = 0.7f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.5"))
	float HydraulicErosionRate = 0.18f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.5"))
	float HydraulicDepositionRate = 0.12f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.4"))
	float HydraulicEvaporation = 0.08f;

	UPROPERTY(EditAnywhere, Category="Terrain|Erosion|Hydraulic", meta=(EditCondition="bEnableHydraulicErosion", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="0.1"))
	float HydraulicMinimumSlope = 0.01f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers")
	bool bEnableRivers = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers", meta=(EditCondition="bEnableRivers", ClampMin="0.0", ClampMax="1.0", UIMin="0.25", UIMax="0.9"))
	float RiverFlowThreshold = 0.58f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers", meta=(EditCondition="bEnableRivers", ClampMin="0.001", ClampMax="0.5", UIMin="0.02", UIMax="0.3"))
	float RiverThresholdTransition = 0.14f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers", meta=(EditCondition="bEnableRivers", ClampMin="50.0", ClampMax="10000.0", UIMin="100.0", UIMax="3000.0", Units="cm"))
	float RiverWidth = 850.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers", meta=(EditCondition="bEnableRivers", ClampMin="0.0", ClampMax="3000.0", UIMin="0.0", UIMax="800.0", Units="cm"))
	float RiverDepth = 180.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers", meta=(EditCondition="bEnableRivers", ClampMin="0.1", ClampMax="8.0", UIMin="0.5", UIMax="4.0"))
	float RiverBankFalloff = 1.8f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Rivers", meta=(EditCondition="bEnableRivers", ClampMin="0.1", ClampMax="6.0", UIMin="0.5", UIMax="3.0"))
	float RiverChannelProfile = 1.35f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Floodplain", meta=(EditCondition="bEnableRivers", ClampMin="100.0", ClampMax="10000.0", UIMin="500.0", UIMax="4000.0", Units="cm"))
	float FloodplainWidth = 1800.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Floodplain", meta=(EditCondition="bEnableRivers", ClampMin="0.0", ClampMax="2000.0", UIMin="0.0", UIMax="600.0", Units="cm"))
	float FloodplainMaxRise = 260.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Floodplain", meta=(EditCondition="bEnableRivers", ClampMin="0.1", ClampMax="6.0", UIMin="0.5", UIMax="3.0"))
	float FloodplainFalloff = 1.6f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Floodplain", meta=(EditCondition="bEnableRivers", ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float WetnessStrength = 0.7f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Water", meta=(EditCondition="bEnableRivers"))
	bool bShowRiverWater = true;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Water", meta=(EditCondition="bEnableRivers && bShowRiverWater", ClampMin="0.0", ClampMax="500.0", UIMin="0.0", UIMax="200.0", Units="cm"))
	float RiverWaterOffset = 70.0f;

	UPROPERTY(EditAnywhere, Category="Terrain|Hydrology|Water", meta=(EditCondition="bEnableRivers && bShowRiverWater", ClampMin="0.0", ClampMax="1.0", UIMin="0.1", UIMax="0.9"))
	float RiverWaterMaskThreshold = 0.42f;

	TArray<float> FlowAccumulation;
	TArray<float> RiverMask;
	TArray<float> FloodplainMask;
	TArray<float> WetnessMask;
	TArray<FIntPoint> RiverNetworkEdges;

	TArray<float> LandMask;
	TArray<float> OceanMask;
	TArray<float> CoastMask;
	TArray<float> BathymetryDepthMap;
	TArray<float> ShelfMask;
	TArray<float> ContinentalSlopeMask;
	TArray<float> OceanBasinMask;
	TArray<float> TrenchMask;
	TArray<float> SeamountMask;

	TArray<float> TemperatureMap;
	TArray<float> PrecipitationMap;
	TArray<float> HumidityMap;
	TArray<float> SnowPotentialMap;
	TArray<float> ForestBiomeMask;
	TArray<float> GrasslandBiomeMask;
	TArray<float> AridBiomeMask;
	TArray<float> AlpineBiomeMask;
	TArray<float> WetlandBiomeMask;
	TArray<float> ExposedRockBiomeMask;
	TArray<float> SnowBiomeMask;

	void BuildTerrain();
	void BuildRiverWaterMesh(const FTerrainHeightField& HeightField, float CellSize, float HalfWorldSize);
};
