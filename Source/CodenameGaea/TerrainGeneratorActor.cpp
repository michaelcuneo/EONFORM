#include "TerrainGeneratorActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "TerrainBiomes.h"
#include "TerrainClimate.h"
#include "TerrainContext.h"
#include "TerrainDrainage.h"
#include "TerrainErosion.h"
#include "TerrainGeology.h"
#include "TerrainHeightField.h"
#include "TerrainHydrology.h"
#include "TerrainLandmass.h"
#include "TerrainNoise.h"
#include "TerrainPhysiography.h"
#include "TerrainShaping.h"
#include "TerrainStructure.h"
#include "TerrainWater.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FMeshNormals;

ATerrainGeneratorActor::ATerrainGeneratorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TerrainMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("TerrainMesh"));
	SetRootComponent(TerrainMesh);
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainMesh->SetGenerateOverlapEvents(false);
	TerrainMesh->SetCastShadow(true);

	RiverWaterMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("RiverWaterMesh"));
	RiverWaterMesh->SetupAttachment(TerrainMesh);
	RiverWaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RiverWaterMesh->SetGenerateOverlapEvents(false);
	RiverWaterMesh->SetCastShadow(false);
}

void ATerrainGeneratorActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildTerrain();
}

#if WITH_EDITOR
void ATerrainGeneratorActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	BuildTerrain();
}
#endif

void ATerrainGeneratorActor::Regenerate()
{
	BuildTerrain();
}

void ATerrainGeneratorActor::RandomizeSeed()
{
	Seed = FMath::Rand();
	BuildTerrain();
}

void ATerrainGeneratorActor::BuildTerrain()
{
	if (!TerrainMesh || !RiverWaterMesh)
	{
		return;
	}

	const int32 SafeResolution = FMath::Clamp(Resolution, 2, 1025);
	const float SafeWorldSize = FMath::Max(WorldSize, 1.0f);
	const float SafeHeightScale = FMath::Max(HeightScale, 1.0f);

	FTerrainHeightField HeightField;
	HeightField.Initialize(SafeResolution, SafeWorldSize);

	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = SafeWorldSize / static_cast<float>(SafeResolution - 1);
	const float HalfWorldSize = SafeWorldSize * 0.5f;

	TArray<float> MountainRegionMask;
	TArray<float> FoothillRegionMask;
	TArray<float> PlainsRegionMask;
	MountainRegionMask.SetNumZeroed(NumCells);
	FoothillRegionMask.SetNumZeroed(NumCells);
	PlainsRegionMask.SetNumZeroed(NumCells);

	FTerrainStructuralMaps StructuralMaps;
	FTerrainStructuralSettings StructuralSettings;
	if (bEnableStructuralGeology)
	{
		StructuralSettings.DirectionDegrees = StructureDirection;
		StructuralSettings.DirectionVariation = StructureCurvature;
		StructuralSettings.TectonicCoverage = TectonicCoverage;
		StructuralSettings.UpliftSpacing = UpliftSpacing;
		StructuralSettings.UpliftWidth = UpliftWidth;
		StructuralSettings.UpliftStrength = UpliftStrength;
		StructuralSettings.LongValleySpacing = StructuralValleySpacing;
		StructuralSettings.LongValleyWidth = StructuralValleyWidth;
		StructuralSettings.LongValleyDepth = StructuralValleyDepth;
		StructuralSettings.FaultSpacing = FaultSpacing;
		StructuralSettings.FaultWidth = FaultWidth;
		StructuralSettings.FaultAngleOffsetDegrees = FaultAngleOffset;
		StructuralSettings.FaultWeakness = FaultWeakness;
		FTerrainStructure::Build(HeightField, Seed, StructuralSettings, StructuralMaps);
	}

	const bool bHasStructure = StructuralMaps.IsValidFor(HeightField);

	FTerrainLandmassSettings LandmassSettings;
	LandmassSettings.bIsland = bIsland;
	LandmassSettings.bArchipelago = bArchipelago;
	LandmassSettings.CoastScale = CoastScale;
	LandmassSettings.CoastIrregularity = CoastIrregularity;
	LandmassSettings.LandCoverage = LandCoverage;
	LandmassSettings.ShelfWidth = ShelfWidth;
	LandmassSettings.ShelfDepth = ShelfDepth;
	LandmassSettings.ContinentalSlopeWidth = ContinentalSlopeWidth;
	LandmassSettings.BasinDepth = BasinDepth;
	LandmassSettings.BasinRelief = BasinRelief;
	LandmassSettings.TrenchDepth = TrenchDepth;
	LandmassSettings.SeamountScale = SeamountScale;
	LandmassSettings.SeamountHeight = SeamountHeight;

	FTerrainLandmassMaps LandmassMaps;
	FTerrainLandmass::Build(
		HeightField,
		bHasStructure ? &StructuralMaps : nullptr,
		Seed,
		LandmassSettings,
		LandmassMaps);
	const bool bHasLandmass = LandmassMaps.IsValidFor(HeightField);

	FTerrainFractalNoiseSettings BaseSettings{ Frequency, Octaves, Persistence, Lacunarity };
	FTerrainFractalNoiseSettings MacroSettings{ MacroFrequency, MacroOctaves, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings WarpSettings{ WarpFrequency, 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings RidgeSettings{ RidgeFrequency, RidgeOctaves, Persistence, Lacunarity };
	FTerrainFractalNoiseSettings FoothillSettings{ FoothillFrequency, 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings ValleySettings{ ValleyFrequency, 2, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings PlainsSettings{ PlainsRollingFrequency, 3, 0.5f, 2.0f };

	const FVector2D BaseOffset = FTerrainNoise::MakeSeedOffset(Seed, 0);
	const FVector2D MacroOffset = FTerrainNoise::MakeSeedOffset(Seed, 17);
	const FVector2D WarpXOffset = FTerrainNoise::MakeSeedOffset(Seed, 101);
	const FVector2D WarpYOffset = FTerrainNoise::MakeSeedOffset(Seed, 202);
	const FVector2D RidgeOffset = FTerrainNoise::MakeSeedOffset(Seed, 303);
	const FVector2D FoothillOffset = FTerrainNoise::MakeSeedOffset(Seed, 404);
	const FVector2D ValleyOffset = FTerrainNoise::MakeSeedOffset(Seed, 505);
	const FVector2D PlainsOffset = FTerrainNoise::MakeSeedOffset(Seed, 606);

	const float StructuralValleyDepthNormalized = StructuralValleyDepth / SafeHeightScale;

	for (int32 Y = 0; Y < SafeResolution; ++Y)
	{
		for (int32 X = 0; X < SafeResolution; ++X)
		{
			const int32 Index = HeightField.Index(X, Y);
			const FVector2D WorldPosition(
				static_cast<float>(X) * CellSize - HalfWorldSize,
				static_cast<float>(Y) * CellSize - HalfWorldSize);

			const float LandInfluence = bHasLandmass ? LandmassMaps.LandInfluence[Index] : 1.0f;
			const float BaseElevation = bHasLandmass ? LandmassMaps.BaseElevationCm[Index] / SafeHeightScale : 0.0f;
			const float StructuralUplift = bHasStructure ? StructuralMaps.Uplift[Index] : 0.0f;
			const float StructuralValley = bHasStructure ? StructuralMaps.LongValley[Index] : 0.0f;

			float PreliminaryMacro = 0.0f;
			float PreliminaryMountain = bEnableMountainMask ? 0.0f : 1.0f;
			float PreliminaryFoothill = 0.0f;

			if (bEnableMacroShape)
			{
				PreliminaryMacro = FTerrainNoise::SampleFractal(WorldPosition, MacroOffset, MacroSettings);
				PreliminaryMacro = FTerrainShaping::ApplySignedPower(PreliminaryMacro, MacroContrast);
				if (bEnableMountainMask)
				{
					PreliminaryMountain = FTerrainShaping::BuildMountainMask(PreliminaryMacro, MountainThreshold, MountainTransition);
				}
			}

			if (bHasStructure)
			{
				PreliminaryMountain = FMath::Clamp(FMath::Max(PreliminaryMountain, StructuralUplift * 0.78f), 0.0f, 1.0f);
			}
			PreliminaryMountain *= LandInfluence;

			if (bEnableFoothills)
			{
				PreliminaryFoothill = FTerrainShaping::BuildFoothillMask(PreliminaryMountain, FoothillWidth) * LandInfluence;
			}

			FVector2D SamplePosition = WorldPosition;
			if (bEnableDomainWarp && WarpStrength > 0.0f)
			{
				const float WarpX = FTerrainNoise::SampleFractal(WorldPosition, WarpXOffset, WarpSettings);
				const float WarpY = FTerrainNoise::SampleFractal(WorldPosition, WarpYOffset, WarpSettings);
				float WarpMask = 1.0f;
				if (bUseNaturalProcessMasks && bEnableMacroShape)
				{
					const float NaturalWarpRegion = FMath::Clamp(PreliminaryMountain + PreliminaryFoothill * 0.7f + 0.08f, 0.0f, 1.0f);
					WarpMask = FMath::Lerp(1.0f, NaturalWarpRegion, FMath::Clamp(WarpRegionality, 0.0f, 1.0f));
				}
				SamplePosition += FVector2D(WarpX, WarpY) * (WarpStrength * WarpMask * LandInfluence);
			}

			const float BaseHeightNoise = FTerrainNoise::SampleFractal(SamplePosition, BaseOffset, BaseSettings);
			float MacroHeight = PreliminaryMacro;
			float MountainMask = PreliminaryMountain;

			if (bEnableMacroShape)
			{
				MacroHeight = FTerrainNoise::SampleFractal(SamplePosition, MacroOffset, MacroSettings);
				MacroHeight = FTerrainShaping::ApplySignedPower(MacroHeight, MacroContrast);
				if (bEnableMountainMask)
				{
					MountainMask = FTerrainShaping::BuildMountainMask(MacroHeight, MountainThreshold, MountainTransition);
				}
			}
			if (bHasStructure)
			{
				MountainMask = FMath::Clamp(FMath::Max(MountainMask, StructuralUplift * 0.78f), 0.0f, 1.0f);
			}
			MountainMask *= LandInfluence;

			const float FoothillMask = bEnableFoothills
				? FTerrainShaping::BuildFoothillMask(MountainMask, FoothillWidth) * LandInfluence
				: 0.0f;
			const float PlainsMask = FMath::Clamp((1.0f - MountainMask) * (1.0f - FoothillMask * 0.65f) * LandInfluence, 0.0f, 1.0f);

			MountainRegionMask[Index] = MountainMask;
			FoothillRegionMask[Index] = FoothillMask;
			PlainsRegionMask[Index] = PlainsMask;

			float TerrestrialRelief = BaseHeightNoise * 0.38f;
			if (bEnableMacroShape)
			{
				TerrestrialRelief += MacroHeight * MacroStrength;
			}
			if (bHasStructure)
			{
				TerrestrialRelief += StructuralUplift * UpliftStrength;
				TerrestrialRelief -= StructuralValley * StructuralValleyDepthNormalized;
			}
			if (bEnableRidges && RidgeStrength > 0.0f)
			{
				const float Ridge = FTerrainNoise::SampleRidged(SamplePosition, RidgeOffset, RidgeSettings, RidgeSharpness);
				TerrestrialRelief += (Ridge * 2.0f - 1.0f) * RidgeStrength * MountainMask;
			}
			if (bEnableFoothills && FoothillStrength > 0.0f)
			{
				const float FoothillNoise = FTerrainNoise::SampleFractal(SamplePosition, FoothillOffset, FoothillSettings);
				TerrestrialRelief += FoothillNoise * FoothillStrength * FoothillMask;
			}
			if (bEnableValleys && ValleyDepth > 0.0f)
			{
				const float ValleyNoise = FTerrainNoise::SampleFractal(SamplePosition, ValleyOffset, ValleySettings);
				const float ValleyMask = FTerrainShaping::BuildValleyMask(ValleyNoise, ValleyWidth, ValleySharpness);
				const float ValleyLandMask = FMath::Clamp(PlainsMask + FoothillMask * 0.45f, 0.0f, 1.0f);
				const float StructuralPreference = bHasStructure ? FMath::Lerp(0.65f, 1.35f, StructuralValley) : 1.0f;
				TerrestrialRelief -= ValleyMask * ValleyDepth * ValleyLandMask * StructuralPreference;
			}
			if (bEnablePlains && PlainsStrength > 0.0f)
			{
				const float FlattenedHeight = FTerrainShaping::ApplySignedPower(TerrestrialRelief, PlainsFlattenExponent);
				const float RollingNoise = FTerrainNoise::SampleFractal(SamplePosition, PlainsOffset, PlainsSettings);
				const float PlainsTarget = FlattenedHeight + RollingNoise * PlainsRollingStrength;
				TerrestrialRelief = FMath::Lerp(TerrestrialRelief, PlainsTarget, PlainsMask * PlainsStrength);
			}

			HeightField.At(X, Y) = BaseElevation + TerrestrialRelief * LandInfluence;
		}
	}

	if (bHasLandmass)
	{
		FTerrainLandmass::RefreshSeaLevelClassification(HeightField, SafeHeightScale, LandmassSettings, LandmassMaps);
	}

	// Establish broad physiography before any erosional detail pass. The drainage
	// graph provides connected valley hierarchy; the physiography pass turns that
	// hierarchy into uplands, hillslopes, lowlands, valley floors and benches.
	FTerrainDrainageSettings InitialDrainageSettings;
	FTerrainDrainageMaps InitialDrainageMaps;
	FTerrainPhysiographyMaps PhysiographyMaps;
	if (FTerrainDrainage::Build(HeightField, SafeHeightScale, InitialDrainageSettings, InitialDrainageMaps))
	{
		FTerrainPhysiographySettings PhysiographySettings;
		PhysiographySettings.RegionalScaleCm = FMath::Max(SafeWorldSize * 0.16f, CellSize * 12.0f);
		PhysiographySettings.ValleyWidthCm = FMath::Max(SafeWorldSize * 0.028f, CellSize * 2.5f);
		PhysiographySettings.ValleyDepthCm = FMath::Min(SafeHeightScale * 0.07f, SafeWorldSize * 0.012f);

		if (FTerrainPhysiography::Apply(
			HeightField,
			SafeHeightScale,
			InitialDrainageMaps,
			bHasStructure ? &StructuralMaps : nullptr,
			PhysiographySettings,
			PhysiographyMaps))
		{
			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				MountainRegionMask[Index] = FMath::Clamp(
					FMath::Max(MountainRegionMask[Index] * 0.55f, PhysiographyMaps.Upland[Index] * 0.9f),
					0.0f,
					1.0f);
				FoothillRegionMask[Index] = FMath::Clamp(
					FMath::Max(FoothillRegionMask[Index] * 0.5f, PhysiographyMaps.Hillslope[Index]),
					0.0f,
					1.0f);
				PlainsRegionMask[Index] = FMath::Clamp(
					FMath::Max(PlainsRegionMask[Index] * 0.45f, FMath::Max(PhysiographyMaps.Lowland[Index], PhysiographyMaps.ValleyFloor[Index])),
					0.0f,
					1.0f);
			}
		}
	}

	if (bHasLandmass)
	{
		FTerrainLandmass::RefreshSeaLevelClassification(HeightField, SafeHeightScale, LandmassSettings, LandmassMaps);
	}

	FTerrainProcessMaskSettings NaturalSettings;
	NaturalSettings.ThermalRegionality = bUseNaturalProcessMasks ? ThermalRegionality : 0.0f;
	NaturalSettings.HydraulicRegionality = bUseNaturalProcessMasks ? HydraulicRegionality : 0.0f;
	NaturalSettings.RainfallHighlandBias = RainfallHighlandBias;
	NaturalSettings.EvaporationLowlandBias = EvaporationLowlandBias;

	FTerrainContextMaps TerrainContext;
	FTerrainProcessMasks ProcessMasks;
	FTerrainGeologyMaps GeologyMaps;
	FTerrainGeologySettings GeologySettings;
	FTerrainClimateMaps ClimateMaps;
	FTerrainClimateSettings ClimateSettings;
	ClimateSettings.PrevailingWindDirectionDegrees = PrevailingWindDirection;
	ClimateSettings.BaseTemperatureC = BaseTemperatureC;
	ClimateSettings.LapseRateCPerKm = TemperatureLapseRate;
	ClimateSettings.BaseHumidity = BaseHumidity;
	ClimateSettings.OrographicStrength = OrographicStrength;
	ClimateSettings.RainShadowStrength = RainShadowStrength;
	ClimateSettings.MoistureRecovery = MoistureRecovery;

	FTerrainContext::Analyze(HeightField, SafeHeightScale, MountainRegionMask, FoothillRegionMask, PlainsRegionMask, TerrainContext);
	FTerrainGeology::Build(HeightField, TerrainContext, bHasStructure ? &StructuralMaps : nullptr, Seed, GeologySettings, GeologyMaps);
	FTerrainContext::BuildProcessMasks(TerrainContext, HeightField, ThermalTalusAngle, NaturalSettings, ProcessMasks);

	if (bEnableThermalErosion && ThermalIterations > 0 && ThermalStrength > 0.0f)
	{
		FTerrainThermalErosionSettings ThermalSettings;
		ThermalSettings.Iterations = ThermalIterations;
		ThermalSettings.TalusAngleDegrees = ThermalTalusAngle;
		ThermalSettings.Strength = ThermalStrength;
		const TArray<float>* ThermalMask = ProcessMasks.IsValidFor(HeightField) ? &ProcessMasks.Thermal : nullptr;
		const TArray<float>* Hardness = GeologyMaps.IsValidFor(HeightField) ? &GeologyMaps.RockHardness : nullptr;
		FTerrainErosion::ApplyThermal(HeightField, SafeHeightScale, ThermalSettings, ThermalMask, Hardness);
	}

	if (bHasLandmass)
	{
		FTerrainLandmass::RefreshSeaLevelClassification(HeightField, SafeHeightScale, LandmassSettings, LandmassMaps);
	}

	FTerrainContext::Analyze(HeightField, SafeHeightScale, MountainRegionMask, FoothillRegionMask, PlainsRegionMask, TerrainContext);
	FTerrainGeology::Build(HeightField, TerrainContext, bHasStructure ? &StructuralMaps : nullptr, Seed, GeologySettings, GeologyMaps);
	FTerrainContext::BuildProcessMasks(TerrainContext, HeightField, ThermalTalusAngle, NaturalSettings, ProcessMasks);

	if (bEnableClimate)
	{
		FTerrainClimate::Build(HeightField, TerrainContext, SafeHeightScale, ClimateSettings, ClimateMaps);
		if (ClimateMaps.IsValidFor(HeightField) && ProcessMasks.IsValidFor(HeightField))
		{
			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				const float ClimateRain = FMath::Lerp(0.3f, 1.35f, ClimateMaps.Precipitation[Index]);
				const float ClimateEvaporation = FMath::Lerp(0.45f, 1.35f, ClimateMaps.EvaporationPotential[Index]);
				ProcessMasks.Rainfall[Index] = FMath::Clamp(ProcessMasks.Rainfall[Index] * ClimateRain, 0.0f, 1.0f);
				ProcessMasks.Evaporation[Index] = FMath::Clamp(ProcessMasks.Evaporation[Index] * ClimateEvaporation, 0.0f, 1.0f);
			}
		}
	}

	FlowAccumulation.Reset();
	if (bEnableHydraulicErosion && HydraulicIterations > 0)
	{
		FTerrainHydraulicErosionSettings HydraulicSettings;
		HydraulicSettings.Iterations = HydraulicIterations;
		HydraulicSettings.Rainfall = HydraulicRainfall;
		HydraulicSettings.FlowRate = HydraulicFlowRate;
		HydraulicSettings.SedimentCapacity = HydraulicSedimentCapacity;
		HydraulicSettings.ErosionRate = HydraulicErosionRate;
		HydraulicSettings.DepositionRate = HydraulicDepositionRate;
		HydraulicSettings.Evaporation = HydraulicEvaporation;
		HydraulicSettings.MinimumSlope = HydraulicMinimumSlope;

		const bool bHasProcessMasks = ProcessMasks.IsValidFor(HeightField);
		const bool bHasGeology = GeologyMaps.IsValidFor(HeightField);
		FTerrainErosion::ApplyHydraulic(
			HeightField,
			SafeHeightScale,
			HydraulicSettings,
			&FlowAccumulation,
			bHasProcessMasks ? &ProcessMasks.Rainfall : nullptr,
			bHasProcessMasks ? &ProcessMasks.HydraulicErosion : nullptr,
			bHasProcessMasks ? &ProcessMasks.Deposition : nullptr,
			bHasProcessMasks ? &ProcessMasks.Evaporation : nullptr,
			bHasGeology ? &GeologyMaps.RockHardness : nullptr,
			bHasGeology ? &GeologyMaps.SoilDepth : nullptr);
	}

	if (bHasLandmass)
	{
		FTerrainLandmass::RefreshSeaLevelClassification(HeightField, SafeHeightScale, LandmassSettings, LandmassMaps);
	}

	FTerrainDrainageSettings DrainageSettings;
	FTerrainDrainageMaps DrainageMaps;
	const bool bHasDrainage = FTerrainDrainage::Build(
		HeightField,
		SafeHeightScale,
		DrainageSettings,
		DrainageMaps);

	if (bHasDrainage)
	{
		FlowAccumulation = DrainageMaps.FlowAccumulation;
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			if (DrainageMaps.ExteriorOceanMask[Index] > 0.5f)
			{
				FlowAccumulation[Index] = 0.0f;
			}
		}
	}
	else if (bHasLandmass && FlowAccumulation.Num() == NumCells)
	{
		for (int32 Index = 0; Index < NumCells; ++Index)
		{
			FlowAccumulation[Index] *= LandmassMaps.LandMask[Index];
		}
	}

	RiverMask.Reset();
	FloodplainMask.Reset();
	WetnessMask.Reset();
	RiverNetworkEdges.Reset();

	if (bEnableRivers && RiverDepth > 0.0f && FlowAccumulation.Num() == HeightField.Data.Num())
	{
		FTerrainRiverSettings RiverSettings;
		RiverSettings.FlowThreshold = RiverFlowThreshold;
		RiverSettings.ThresholdTransition = RiverThresholdTransition;
		RiverSettings.Width = RiverWidth;
		RiverSettings.Depth = RiverDepth;
		RiverSettings.BankFalloff = RiverBankFalloff;
		RiverSettings.ChannelProfile = RiverChannelProfile;

		if (FTerrainHydrology::BuildRiverMask(HeightField, FlowAccumulation, RiverSettings, RiverMask))
		{
			if (bHasLandmass && RiverMask.Num() == NumCells)
			{
				for (int32 Index = 0; Index < NumCells; ++Index)
				{
					RiverMask[Index] *= LandmassMaps.LandMask[Index];
				}
			}
			FTerrainHydrology::CarveRivers(HeightField, SafeHeightScale, RiverSettings, RiverMask);
			if (bHasDrainage)
			{
				FTerrainHydrology::BuildRiverNetwork(
					HeightField,
					FlowAccumulation,
					DrainageMaps.Receiver,
					RiverSettings,
					RiverNetworkEdges);
			}

			FTerrainFloodplainSettings FloodplainSettings;
			FloodplainSettings.Width = FloodplainWidth;
			FloodplainSettings.MaxRise = FloodplainMaxRise;
			FloodplainSettings.Falloff = FloodplainFalloff;
			FloodplainSettings.WetnessStrength = WetnessStrength;
			FTerrainHydrology::BuildFloodplainMasks(HeightField, RiverMask, SafeHeightScale, FloodplainSettings, FloodplainMask, WetnessMask);
		}
	}

	if (bHasLandmass)
	{
		FTerrainLandmass::RefreshSeaLevelClassification(HeightField, SafeHeightScale, LandmassSettings, LandmassMaps);
		LandMask = LandmassMaps.LandMask;
		OceanMask = LandmassMaps.OceanMask;
		CoastMask = LandmassMaps.CoastMask;
		BathymetryDepthMap = LandmassMaps.BathymetryDepthCm;
		ShelfMask = LandmassMaps.ShelfMask;
		ContinentalSlopeMask = LandmassMaps.ContinentalSlopeMask;
		OceanBasinMask = LandmassMaps.OceanBasinMask;
		TrenchMask = LandmassMaps.TrenchMask;
		SeamountMask = LandmassMaps.SeamountMask;
	}
	else
	{
		LandMask.Reset(); OceanMask.Reset(); CoastMask.Reset(); BathymetryDepthMap.Reset();
		ShelfMask.Reset(); ContinentalSlopeMask.Reset(); OceanBasinMask.Reset(); TrenchMask.Reset(); SeamountMask.Reset();
	}

	TemperatureMap.Reset();
	PrecipitationMap.Reset();
	HumidityMap.Reset();
	SnowPotentialMap.Reset();
	ForestBiomeMask.Reset();
	GrasslandBiomeMask.Reset();
	AridBiomeMask.Reset();
	AlpineBiomeMask.Reset();
	WetlandBiomeMask.Reset();
	ExposedRockBiomeMask.Reset();
	SnowBiomeMask.Reset();

	FTerrainContext::Analyze(HeightField, SafeHeightScale, MountainRegionMask, FoothillRegionMask, PlainsRegionMask, TerrainContext);
	FTerrainGeology::Build(HeightField, TerrainContext, bHasStructure ? &StructuralMaps : nullptr, Seed, GeologySettings, GeologyMaps);

	if (bEnableClimate)
	{
		FTerrainClimate::Build(HeightField, TerrainContext, SafeHeightScale, ClimateSettings, ClimateMaps);
		if (ClimateMaps.IsValidFor(HeightField))
		{
			TemperatureMap = ClimateMaps.TemperatureC;
			PrecipitationMap = ClimateMaps.Precipitation;
			HumidityMap = ClimateMaps.Humidity;
			SnowPotentialMap = ClimateMaps.SnowPotential;
		}
	}

	if (bEnableBiomes && ClimateMaps.IsValidFor(HeightField) && GeologyMaps.IsValidFor(HeightField))
	{
		FTerrainBiomeSettings BiomeSettings;
		BiomeSettings.ForestMoistureThreshold = ForestMoistureThreshold;
		BiomeSettings.ForestMaxSlopeDegrees = ForestMaxSlope;
		BiomeSettings.AridMoistureThreshold = AridMoistureThreshold;
		BiomeSettings.AlpineElevationThreshold = AlpineElevationThreshold;
		BiomeSettings.WetlandWetnessThreshold = WetlandWetnessThreshold;

		FTerrainBiomeMaps BiomeMaps;
		FTerrainBiomes::Build(HeightField, TerrainContext, ClimateMaps, GeologyMaps, WetnessMask, RiverMask, BiomeSettings, BiomeMaps);
		if (BiomeMaps.IsValidFor(HeightField))
		{
			ForestBiomeMask = MoveTemp(BiomeMaps.Forest);
			GrasslandBiomeMask = MoveTemp(BiomeMaps.Grassland);
			AridBiomeMask = MoveTemp(BiomeMaps.Arid);
			AlpineBiomeMask = MoveTemp(BiomeMaps.Alpine);
			WetlandBiomeMask = MoveTemp(BiomeMaps.Wetland);
			ExposedRockBiomeMask = MoveTemp(BiomeMaps.ExposedRock);
			SnowBiomeMask = MoveTemp(BiomeMaps.Snow);

			if (bHasLandmass)
			{
				for (int32 Index = 0; Index < NumCells; ++Index)
				{
					const float Land = LandmassMaps.LandMask[Index];
					ForestBiomeMask[Index] *= Land;
					GrasslandBiomeMask[Index] *= Land;
					AridBiomeMask[Index] *= Land;
					AlpineBiomeMask[Index] *= Land;
					WetlandBiomeMask[Index] *= Land;
					ExposedRockBiomeMask[Index] *= Land;
					SnowBiomeMask[Index] *= Land;
				}
			}
		}
	}

	FTerrainWater::UpdateOceanFromZeroContour(HeightField, GetActorTransform(), GetWorld());

	FDynamicMesh3 Mesh(true, false, false, false);
	TArray<int32> VertexIds;
	VertexIds.SetNumUninitialized(SafeResolution * SafeResolution);

	for (int32 Y = 0; Y < SafeResolution; ++Y)
	{
		for (int32 X = 0; X < SafeResolution; ++X)
		{
			const float LocalX = static_cast<float>(X) * CellSize - HalfWorldSize;
			const float LocalY = static_cast<float>(Y) * CellSize - HalfWorldSize;
			const float LocalZ = HeightField.At(X, Y) * SafeHeightScale;
			const int32 VertexId = Mesh.AppendVertex(FVector3d(LocalX, LocalY, LocalZ));
			VertexIds[Y * SafeResolution + X] = VertexId;
		}
	}

	for (int32 Y = 0; Y < SafeResolution - 1; ++Y)
	{
		for (int32 X = 0; X < SafeResolution - 1; ++X)
		{
			const int32 A = VertexIds[Y * SafeResolution + X];
			const int32 B = VertexIds[Y * SafeResolution + X + 1];
			const int32 C = VertexIds[(Y + 1) * SafeResolution + X];
			const int32 D = VertexIds[(Y + 1) * SafeResolution + X + 1];
			Mesh.AppendTriangle(A, D, B, 0);
			Mesh.AppendTriangle(A, C, D, 0);
		}
	}

	FMeshNormals::QuickComputeVertexNormals(Mesh);
	TerrainMesh->SetMesh(MoveTemp(Mesh));
	BuildRiverWaterMesh(HeightField, CellSize, HalfWorldSize);
}

void ATerrainGeneratorActor::BuildRiverWaterMesh(
	const FTerrainHeightField& HeightField,
	float CellSize,
	float HalfWorldSize)
{
	FDynamicMesh3 WaterMesh(true, false, false, false);

	if (!bEnableRivers || !bShowRiverWater || RiverMask.Num() != HeightField.Data.Num())
	{
		RiverWaterMesh->SetMesh(MoveTemp(WaterMesh));
		return;
	}

	const int32 SafeResolution = HeightField.Resolution;
	const float MaskThreshold = FMath::Clamp(RiverWaterMaskThreshold, 0.0f, 1.0f);
	TArray<int32> WaterVertexIds;
	WaterVertexIds.SetNumUninitialized(SafeResolution * SafeResolution);

	for (int32 Y = 0; Y < SafeResolution; ++Y)
	{
		for (int32 X = 0; X < SafeResolution; ++X)
		{
			const float LocalX = static_cast<float>(X) * CellSize - HalfWorldSize;
			const float LocalY = static_cast<float>(Y) * CellSize - HalfWorldSize;
			const float RiverSurface = HeightField.At(X, Y) * FMath::Max(HeightScale, 1.0f) + RiverWaterOffset;
			const float LocalZ = FMath::Max(RiverSurface, 0.0f);
			WaterVertexIds[HeightField.Index(X, Y)] = WaterMesh.AppendVertex(FVector3d(LocalX, LocalY, LocalZ));
		}
	}

	for (int32 Y = 0; Y < SafeResolution - 1; ++Y)
	{
		for (int32 X = 0; X < SafeResolution - 1; ++X)
		{
			const int32 IA = HeightField.Index(X, Y);
			const int32 IB = HeightField.Index(X + 1, Y);
			const int32 IC = HeightField.Index(X, Y + 1);
			const int32 ID = HeightField.Index(X + 1, Y + 1);
			const float CellMask = (RiverMask[IA] + RiverMask[IB] + RiverMask[IC] + RiverMask[ID]) * 0.25f;
			if (CellMask < MaskThreshold)
			{
				continue;
			}
			const int32 A = WaterVertexIds[IA];
			const int32 B = WaterVertexIds[IB];
			const int32 C = WaterVertexIds[IC];
			const int32 D = WaterVertexIds[ID];
			WaterMesh.AppendTriangle(A, D, B, 0);
			WaterMesh.AppendTriangle(A, C, D, 0);
		}
	}

	if (WaterMesh.TriangleCount() > 0)
	{
		FMeshNormals::QuickComputeVertexNormals(WaterMesh);
	}
	RiverWaterMesh->SetMesh(MoveTemp(WaterMesh));
}
