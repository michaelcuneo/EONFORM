#include "TerrainGeneratorActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopedSlowTask.h"
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
#include "TerrainParallel.h"
#include "TerrainPhysiography.h"
#include "TerrainShaping.h"
#include "TerrainStructure.h"
#include "TerrainWater.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FMeshNormals;

DEFINE_LOG_CATEGORY_STATIC(LogTerrainGenerator, Log, All);

namespace
{
	constexpr float TerrainReferenceWorldSizeCm = 50000.0f;

	class FTerrainBuildProgress
	{
	public:
		FTerrainBuildProgress(int32 InResolution, float InCellSizeCm)
			: Resolution(InResolution)
			, CellSizeCm(InCellSizeCm)
			, StartTime(FPlatformTime::Seconds())
			, StageStartTime(StartTime)
			, SlowTask(100.0f, FText::FromString(TEXT("Generating terrain...")))
		{
#if WITH_EDITOR
			if (Resolution >= 1025)
			{
				SlowTask.MakeDialogDelayed(0.35f, false, false);
			}
#endif
		}

		void Stage(const TCHAR* StageName, float WorkUnits)
		{
			FinishActiveStage();

			const double Now = FPlatformTime::Seconds();
			const double Elapsed = Now - StartTime;
			const double Fraction = CompletedWork / 100.0;
			const double Remaining = Fraction > 0.001
				? FMath::Max(Elapsed / Fraction - Elapsed, 0.0)
				: -1.0;

			const FString EtaText = Remaining >= 0.0
				? FString::Printf(TEXT("~%.1fs remaining"), Remaining)
				: FString(TEXT("estimating remaining time"));
			const FString Message = FString::Printf(
				TEXT("%s | %.0f%% | %.1fs elapsed | %s | %d x %d | %.2f m/cell"),
				StageName,
				CompletedWork,
				Elapsed,
				*EtaText,
				Resolution,
				Resolution,
				CellSizeCm / 100.0f);

			SlowTask.EnterProgressFrame(WorkUnits, FText::FromString(Message));
			SlowTask.ForceRefresh();

			ActiveStage = StageName;
			ActiveWork = WorkUnits;
			StageStartTime = Now;
			UE_LOG(LogTerrainGenerator, Display, TEXT("Terrain stage: %s"), StageName);
		}

		double Complete()
		{
			FinishActiveStage();
			SlowTask.ForceRefresh();
			const double Elapsed = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogTerrainGenerator, Display, TEXT("Terrain generation complete in %.2f seconds"), Elapsed);
			return Elapsed;
		}

	private:
		void FinishActiveStage()
		{
			if (ActiveStage.IsEmpty())
			{
				return;
			}

			const double Now = FPlatformTime::Seconds();
			const double StageSeconds = Now - StageStartTime;
			CompletedWork = FMath::Min(CompletedWork + ActiveWork, 100.0f);
			UE_LOG(
				LogTerrainGenerator,
				Display,
				TEXT("Terrain stage complete: %s (%.2fs, %.0f%% overall)"),
				*ActiveStage,
				StageSeconds,
				CompletedWork);
			ActiveStage.Reset();
			ActiveWork = 0.0f;
		}

		int32 Resolution = 0;
		float CellSizeCm = 0.0f;
		double StartTime = 0.0;
		double StageStartTime = 0.0;
		float CompletedWork = 0.0f;
		float ActiveWork = 0.0f;
		FString ActiveStage;
		FScopedSlowTask SlowTask;
	};
}

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

	// High-resolution terrain should regenerate when the user commits a value, not
	// on every intermediate slider/spin-box update while the value is being edited.
	if ((PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) == 0)
	{
		BuildTerrain();
	}
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

	const int32 SafeResolution = FMath::Clamp(Resolution, 2, 4097);
	const float SafeWorldSize = FMath::Max(WorldSize, 1.0f);
	const float SafeHeightScale = FMath::Max(HeightScale, 1.0f);

	FTerrainHeightField HeightField;
	HeightField.Initialize(SafeResolution, SafeWorldSize);

	const int32 NumCells = HeightField.Data.Num();
	const float CellSize = SafeWorldSize / static_cast<float>(SafeResolution - 1);
	const float HalfWorldSize = SafeWorldSize * 0.5f;

	// Horizontal feature parameters were authored against the original 500 m test
	// world. Preserve their proportions as WorldSize changes instead of keeping tiny
	// centimetre-scale ridges/faults inside a many-kilometre regional terrain.
	const float HorizontalFeatureScale = FMath::Max(SafeWorldSize / TerrainReferenceWorldSizeCm, 0.01f);
	const float InverseHorizontalFeatureScale = 1.0f / HorizontalFeatureScale;
	const float ChannelFeatureScale = FMath::Sqrt(HorizontalFeatureScale);
	const auto ScaleHorizontal = [HorizontalFeatureScale](float Value)
	{
		return Value * HorizontalFeatureScale;
	};
	const auto ScaleChannel = [ChannelFeatureScale](float Value)
	{
		return Value * ChannelFeatureScale;
	};
	const auto ScaleFrequency = [InverseHorizontalFeatureScale](float Value)
	{
		return Value * InverseHorizontalFeatureScale;
	};

	UE_LOG(
		LogTerrainGenerator,
		Display,
		TEXT("Terrain build: %d x %d, world %.2f km, cell %.2f m, height scale %.1f m, horizontal feature scale %.2fx"),
		SafeResolution,
		SafeResolution,
		SafeWorldSize / 100000.0f,
		CellSize / 100.0f,
		SafeHeightScale / 100.0f,
		HorizontalFeatureScale);

	FTerrainBuildProgress Progress(SafeResolution, CellSize);

	TArray<float> MountainRegionMask;
	TArray<float> FoothillRegionMask;
	TArray<float> PlainsRegionMask;
	MountainRegionMask.SetNumZeroed(NumCells);
	FoothillRegionMask.SetNumZeroed(NumCells);
	PlainsRegionMask.SetNumZeroed(NumCells);

	Progress.Stage(TEXT("Structural geology"), 4.0f);
	FTerrainStructuralMaps StructuralMaps;
	FTerrainStructuralSettings StructuralSettings;
	if (bEnableStructuralGeology)
	{
		StructuralSettings.DirectionDegrees = StructureDirection;
		StructuralSettings.DirectionVariation = StructureCurvature;
		StructuralSettings.TectonicCoverage = TectonicCoverage;
		StructuralSettings.UpliftSpacing = ScaleHorizontal(UpliftSpacing);
		StructuralSettings.UpliftWidth = ScaleHorizontal(UpliftWidth);
		StructuralSettings.UpliftStrength = UpliftStrength;
		StructuralSettings.LongValleySpacing = ScaleHorizontal(StructuralValleySpacing);
		StructuralSettings.LongValleyWidth = ScaleHorizontal(StructuralValleyWidth);
		StructuralSettings.LongValleyDepth = StructuralValleyDepth;
		StructuralSettings.FaultSpacing = ScaleHorizontal(FaultSpacing);
		StructuralSettings.FaultWidth = ScaleHorizontal(FaultWidth);
		StructuralSettings.FaultAngleOffsetDegrees = FaultAngleOffset;
		StructuralSettings.FaultWeakness = FaultWeakness;
		StructuralSettings.BeddingSpacing = ScaleHorizontal(StructuralSettings.BeddingSpacing);
		FTerrainStructure::Build(HeightField, Seed, StructuralSettings, StructuralMaps);
	}

	const bool bHasStructure = StructuralMaps.IsValidFor(HeightField);

	FTerrainLandmassSettings LandmassSettings;
	LandmassSettings.bIsland = bIsland;
	LandmassSettings.bArchipelago = bArchipelago;
	LandmassSettings.CoastScale = ScaleHorizontal(CoastScale);
	LandmassSettings.CoastIrregularity = CoastIrregularity;
	LandmassSettings.LandCoverage = LandCoverage;
	LandmassSettings.ShelfWidth = ScaleHorizontal(ShelfWidth);
	LandmassSettings.ShelfDepth = ShelfDepth;
	LandmassSettings.ContinentalSlopeWidth = ScaleHorizontal(ContinentalSlopeWidth);
	LandmassSettings.BasinDepth = BasinDepth;
	LandmassSettings.BasinRelief = BasinRelief;
	LandmassSettings.TrenchDepth = TrenchDepth;
	LandmassSettings.SeamountScale = ScaleHorizontal(SeamountScale);
	LandmassSettings.SeamountHeight = SeamountHeight;

	FTerrainLandmassMaps LandmassMaps;
	FTerrainLandmass::Build(
		HeightField,
		bHasStructure ? &StructuralMaps : nullptr,
		Seed,
		LandmassSettings,
		LandmassMaps);
	const bool bHasLandmass = LandmassMaps.IsValidFor(HeightField);

	FTerrainFractalNoiseSettings BaseSettings{ ScaleFrequency(Frequency), Octaves, Persistence, Lacunarity };
	FTerrainFractalNoiseSettings MacroSettings{ ScaleFrequency(MacroFrequency), MacroOctaves, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings WarpSettings{ ScaleFrequency(WarpFrequency), 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings RidgeSettings{ ScaleFrequency(RidgeFrequency), RidgeOctaves, Persistence, Lacunarity };
	FTerrainFractalNoiseSettings FoothillSettings{ ScaleFrequency(FoothillFrequency), 3, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings ValleySettings{ ScaleFrequency(ValleyFrequency), 2, 0.5f, 2.0f };
	FTerrainFractalNoiseSettings PlainsSettings{ ScaleFrequency(PlainsRollingFrequency), 3, 0.5f, 2.0f };

	const FVector2D BaseOffset = FTerrainNoise::MakeSeedOffset(Seed, 0);
	const FVector2D MacroOffset = FTerrainNoise::MakeSeedOffset(Seed, 17);
	const FVector2D WarpXOffset = FTerrainNoise::MakeSeedOffset(Seed, 101);
	const FVector2D WarpYOffset = FTerrainNoise::MakeSeedOffset(Seed, 202);
	const FVector2D RidgeOffset = FTerrainNoise::MakeSeedOffset(Seed, 303);
	const FVector2D FoothillOffset = FTerrainNoise::MakeSeedOffset(Seed, 404);
	const FVector2D ValleyOffset = FTerrainNoise::MakeSeedOffset(Seed, 505);
	const FVector2D PlainsOffset = FTerrainNoise::MakeSeedOffset(Seed, 606);

	const float StructuralValleyDepthNormalized = StructuralValleyDepth / SafeHeightScale;
	const float ScaledWarpStrength = ScaleHorizontal(WarpStrength);

	Progress.Stage(TEXT("Terrain synthesis"), 18.0f);
	TerrainParallel::ForRows(TEXT("TerrainSynthesis"), SafeResolution, [&](int32 StartY, int32 EndY)
	{
		for (int32 Y = StartY; Y < EndY; ++Y)
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
				if (bEnableDomainWarp && ScaledWarpStrength > 0.0f)
				{
					const float WarpX = FTerrainNoise::SampleFractal(WorldPosition, WarpXOffset, WarpSettings);
					const float WarpY = FTerrainNoise::SampleFractal(WorldPosition, WarpYOffset, WarpSettings);
					float WarpMask = 1.0f;
					if (bUseNaturalProcessMasks && bEnableMacroShape)
					{
						const float NaturalWarpRegion = FMath::Clamp(PreliminaryMountain + PreliminaryFoothill * 0.7f + 0.08f, 0.0f, 1.0f);
						WarpMask = FMath::Lerp(1.0f, NaturalWarpRegion, FMath::Clamp(WarpRegionality, 0.0f, 1.0f));
					}
					SamplePosition += FVector2D(WarpX, WarpY) * (ScaledWarpStrength * WarpMask * LandInfluence);
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
	});

	Progress.Stage(TEXT("Coastline and signed DEM"), 10.0f);
	if (bHasLandmass)
	{
		FTerrainLandmass::RefreshSeaLevelClassification(HeightField, SafeHeightScale, LandmassSettings, LandmassMaps);
	}

	Progress.Stage(TEXT("Initial drainage and physiography"), 12.0f);
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
			TerrainParallel::ForRange(TEXT("TerrainPhysiographyMasks"), NumCells, 32768, [&](int32 Start, int32 End)
			{
				for (int32 Index = Start; Index < End; ++Index)
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
			});
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
	GeologySettings.Frequency = ScaleFrequency(GeologySettings.Frequency);
	FTerrainClimateMaps ClimateMaps;
	FTerrainClimateSettings ClimateSettings;
	ClimateSettings.PrevailingWindDirectionDegrees = PrevailingWindDirection;
	ClimateSettings.BaseTemperatureC = BaseTemperatureC;
	ClimateSettings.LapseRateCPerKm = TemperatureLapseRate;
	ClimateSettings.BaseHumidity = BaseHumidity;
	ClimateSettings.OrographicStrength = OrographicStrength;
	ClimateSettings.RainShadowStrength = RainShadowStrength;
	ClimateSettings.MoistureRecovery = MoistureRecovery;

	Progress.Stage(TEXT("Terrain analysis and geology"), 7.0f);
	FTerrainContext::Analyze(HeightField, SafeHeightScale, MountainRegionMask, FoothillRegionMask, PlainsRegionMask, TerrainContext);
	FTerrainGeology::Build(HeightField, TerrainContext, bHasStructure ? &StructuralMaps : nullptr, Seed, GeologySettings, GeologyMaps);
	FTerrainContext::BuildProcessMasks(TerrainContext, HeightField, ThermalTalusAngle, NaturalSettings, ProcessMasks);

	Progress.Stage(TEXT("Thermal erosion"), 10.0f);
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

	Progress.Stage(TEXT("Climate and process preparation"), 6.0f);
	FTerrainContext::Analyze(HeightField, SafeHeightScale, MountainRegionMask, FoothillRegionMask, PlainsRegionMask, TerrainContext);
	FTerrainGeology::Build(HeightField, TerrainContext, bHasStructure ? &StructuralMaps : nullptr, Seed, GeologySettings, GeologyMaps);
	FTerrainContext::BuildProcessMasks(TerrainContext, HeightField, ThermalTalusAngle, NaturalSettings, ProcessMasks);

	if (bEnableClimate)
	{
		FTerrainClimate::Build(HeightField, TerrainContext, SafeHeightScale, ClimateSettings, ClimateMaps);
		if (ClimateMaps.IsValidFor(HeightField) && ProcessMasks.IsValidFor(HeightField))
		{
			TerrainParallel::ForRange(TEXT("TerrainClimateProcessMasks"), NumCells, 32768, [&](int32 Start, int32 End)
			{
				for (int32 Index = Start; Index < End; ++Index)
				{
					const float ClimateRain = FMath::Lerp(0.3f, 1.35f, ClimateMaps.Precipitation[Index]);
					const float ClimateEvaporation = FMath::Lerp(0.45f, 1.35f, ClimateMaps.EvaporationPotential[Index]);
					ProcessMasks.Rainfall[Index] = FMath::Clamp(ProcessMasks.Rainfall[Index] * ClimateRain, 0.0f, 1.0f);
					ProcessMasks.Evaporation[Index] = FMath::Clamp(ProcessMasks.Evaporation[Index] * ClimateEvaporation, 0.0f, 1.0f);
				}
			});
		}
	}

	Progress.Stage(TEXT("Hydraulic erosion"), 18.0f);
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

	Progress.Stage(TEXT("Authoritative drainage"), 7.0f);
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
		TerrainParallel::ForRange(TEXT("TerrainOceanFlowMask"), NumCells, 32768, [&](int32 Start, int32 End)
		{
			for (int32 Index = Start; Index < End; ++Index)
			{
				if (DrainageMaps.ExteriorOceanMask[Index] > 0.5f)
				{
					FlowAccumulation[Index] = 0.0f;
				}
			}
		});
	}
	else if (bHasLandmass && FlowAccumulation.Num() == NumCells)
	{
		TerrainParallel::ForRange(TEXT("TerrainLandFlowMask"), NumCells, 32768, [&](int32 Start, int32 End)
		{
			for (int32 Index = Start; Index < End; ++Index)
			{
				FlowAccumulation[Index] *= LandmassMaps.LandMask[Index];
			}
		});
	}

	Progress.Stage(TEXT("Rivers and floodplains"), 3.0f);
	RiverMask.Reset();
	FloodplainMask.Reset();
	WetnessMask.Reset();
	RiverNetworkEdges.Reset();

	if (bEnableRivers && RiverDepth > 0.0f && FlowAccumulation.Num() == HeightField.Data.Num())
	{
		FTerrainRiverSettings RiverSettings;
		RiverSettings.FlowThreshold = RiverFlowThreshold;
		RiverSettings.ThresholdTransition = RiverThresholdTransition;
		RiverSettings.Width = ScaleChannel(RiverWidth);
		RiverSettings.Depth = RiverDepth;
		RiverSettings.BankFalloff = RiverBankFalloff;
		RiverSettings.ChannelProfile = RiverChannelProfile;

		if (FTerrainHydrology::BuildRiverMask(HeightField, FlowAccumulation, RiverSettings, RiverMask))
		{
			if (bHasLandmass && RiverMask.Num() == NumCells)
			{
				TerrainParallel::ForRange(TEXT("TerrainRiverLandMask"), NumCells, 32768, [&](int32 Start, int32 End)
				{
					for (int32 Index = Start; Index < End; ++Index)
					{
						RiverMask[Index] *= LandmassMaps.LandMask[Index];
					}
				});
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
			FloodplainSettings.Width = ScaleChannel(FloodplainWidth);
			FloodplainSettings.MaxRise = FloodplainMaxRise;
			FloodplainSettings.Falloff = FloodplainFalloff;
			FloodplainSettings.WetnessStrength = WetnessStrength;
			FTerrainHydrology::BuildFloodplainMasks(HeightField, RiverMask, SafeHeightScale, FloodplainSettings, FloodplainMask, WetnessMask);
		}
	}

	Progress.Stage(TEXT("Final climate and biomes"), 3.0f);
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
				TerrainParallel::ForRange(TEXT("TerrainBiomeLandMask"), NumCells, 32768, [&](int32 Start, int32 End)
				{
					for (int32 Index = Start; Index < End; ++Index)
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
				});
			}
		}
	}

	Progress.Stage(TEXT("Water and mesh construction"), 2.0f);
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

	Progress.Complete();
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
