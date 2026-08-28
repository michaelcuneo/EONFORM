#pragma once

#include "CoreMinimal.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainGenerationPlan.h"
#include "EonformTerrainGraphAsset.h"
#include "EonformTerrainPhysicalMetrics.h"

/** Shared editor-session state for the Terrain Output pane and the active graph asset. */
class FEonformTerrainOutputEditorState
{
public:
	static FEonformTerrainOutputEditorState& Get()
	{
		static FEonformTerrainOutputEditorState State;
		return State;
	}

	const FEonformTerrainGraphOutputSettings& GetSettings() const
	{
		return Settings;
	}

	void Load(const FEonformTerrainGraphOutputSettings& InSettings)
	{
		Settings = InSettings;
		Sanitize();
		PublishPhysicalContext();
		InvalidateGenerationPlan();
		++Revision;
	}

	void Reset()
	{
		Settings = FEonformTerrainGraphOutputSettings();
		PublishPhysicalContext();
		InvalidateGenerationPlan();
		++Revision;
	}

	void SetWorldWidthKilometers(double Value) { Settings.WorldWidthKilometers = FMath::Max(Value, 0.001); PublishPhysicalContext(); InvalidateGenerationPlan(); ++Revision; }
	void SetWorldDepthKilometers(double Value) { Settings.WorldDepthKilometers = FMath::Max(Value, 0.001); PublishPhysicalContext(); InvalidateGenerationPlan(); ++Revision; }
	void SetElevationScaleMeters(double Value) { Settings.ElevationScaleMeters = FMath::Max(Value, 0.001); PublishPhysicalContext(); InvalidateGenerationPlan(); ++Revision; }
	void SetOutputResolution(int32 Value) { Settings.OutputResolution = FMath::Max(Value, 0); InvalidateGenerationPlan(); ++Revision; }
	void SetSectionLayout(EEonformTerrainOutputSectionLayout Value) { Settings.SectionLayout = Value; ++Revision; }
	void SetSectionComplexity(EEonformTerrainOutputComplexity Value) { Settings.SectionComplexity = Value; ++Revision; }
	void SetSectionsX(int32 Value) { Settings.SectionsX = FMath::Max(Value, 1); ++Revision; }
	void SetSectionsY(int32 Value) { Settings.SectionsY = FMath::Max(Value, 1); ++Revision; }
	void SetMeshPartitionDefinition(const FSoftObjectPath& Value) { Settings.MeshPartitionDefinition = Value; ++Revision; }

	uint64 GetRevision() const { return Revision; }

	/**
	 * Publish the latest validated graph recipe and its external evaluation
	 * context for final output generation. This deliberately retains the recipe,
	 * not a full-resolution terrain raster: Mesh Terrain can request spatial
	 * regions from this plan without first materializing the complete world.
	 */
	void SetGenerationPlan(
		const FEonformTerrainRecipe& InRecipe,
		const FEonformTerrainEvaluationContext& InContext,
		uint64 InGraphGeneration)
	{
		GenerationRecipe = InRecipe;
		GenerationContext = InContext;
		GenerationGraphGeneration = InGraphGeneration;
		bGenerationPlanAvailable = GenerationRecipe.Validate(nullptr);
		if (bGenerationPlanAvailable)
		{
			FEonformTerrainGenerationPlanRegistry::Publish(
				GenerationRecipe,
				GenerationContext,
				FMath::Max<uint64>(GenerationGraphGeneration, 1));
		}
		else
		{
			FEonformTerrainGenerationPlanRegistry::Reset();
		}
	}

	bool GetGenerationPlan(
		FEonformTerrainRecipe& OutRecipe,
		FEonformTerrainEvaluationContext& OutContext,
		uint64* OutGraphGeneration = nullptr) const
	{
		if (!bGenerationPlanAvailable) return false;
		OutRecipe = GenerationRecipe;
		OutContext = GenerationContext;
		if (OutGraphGeneration) *OutGraphGeneration = GenerationGraphGeneration;
		return true;
	}

	bool HasGenerationPlan() const { return bGenerationPlanAvailable; }

	void InvalidateGenerationPlan()
	{
		bGenerationPlanAvailable = false;
		GenerationRecipe = FEonformTerrainRecipe();
		GenerationContext = FEonformTerrainEvaluationContext();
		GenerationGraphGeneration = 0;
		FEonformTerrainGenerationPlanRegistry::Reset();
	}

	/** A new graph revision is being evaluated. No snapshot is generation-safe yet. */
	void BeginAnalysis()
	{
		bAnalysisPending = true;
		bAnalysisAvailable = false;
		PublishedAnalysisRevision = 0;
		AnalysisError.Reset();
		InvalidateGenerationPlan();
	}

	/**
	 * The graph evaluator has produced a valid terrain snapshot. It is immediately
	 * safe for preview and Mesh Terrain generation while derived analysis continues.
	 */
	void PublishTerrain(uint64 InPublishedRevision)
	{
		bAnalysisAvailable = InPublishedRevision != 0;
		PublishedAnalysisRevision = InPublishedRevision;
		bAnalysisPending = true;
		AnalysisError.Reset();
	}

	/** Derived analysis finished and republished the enriched terrain snapshot. */
	void CompleteAnalysis(uint64 InPublishedRevision)
	{
		bAnalysisPending = false;
		bAnalysisAvailable = InPublishedRevision != 0;
		PublishedAnalysisRevision = InPublishedRevision;
		AnalysisError.Reset();
	}

	/** Base graph evaluation/publication failed; there is no usable current terrain. */
	void FailAnalysis(const FString& InError)
	{
		bAnalysisPending = false;
		bAnalysisAvailable = false;
		PublishedAnalysisRevision = 0;
		AnalysisError = InError;
		InvalidateGenerationPlan();
	}

	/** Derived analysis failed, but the already-published base terrain stays usable. */
	void FailDerivedAnalysis(const FString& InError)
	{
		bAnalysisPending = false;
		AnalysisError = InError;
	}

	void InvalidateAnalysis()
	{
		bAnalysisPending = false;
		bAnalysisAvailable = false;
		PublishedAnalysisRevision = 0;
		AnalysisError.Reset();
		InvalidateGenerationPlan();
	}

	bool IsAnalysisPending() const { return bAnalysisPending; }
	bool IsAnalysisAvailable() const { return bAnalysisAvailable; }
	uint64 GetPublishedAnalysisRevision() const { return PublishedAnalysisRevision; }
	const FString& GetAnalysisError() const { return AnalysisError; }

private:
	FEonformTerrainOutputEditorState()
	{
		PublishPhysicalContext();
	}

	void Sanitize()
	{
		Settings.WorldWidthKilometers = FMath::Max(Settings.WorldWidthKilometers, 0.001);
		Settings.WorldDepthKilometers = FMath::Max(Settings.WorldDepthKilometers, 0.001);
		Settings.ElevationScaleMeters = FMath::Max(Settings.ElevationScaleMeters, 0.001);
		Settings.OutputResolution = FMath::Max(Settings.OutputResolution, 0);
		Settings.SectionsX = FMath::Max(Settings.SectionsX, 1);
		Settings.SectionsY = FMath::Max(Settings.SectionsY, 1);
	}

	void PublishPhysicalContext() const
	{
		FEonformTerrainPhysicalMetrics Metrics;
		Metrics.WorldWidthMeters = Settings.WorldWidthKilometers * 1000.0;
		Metrics.WorldDepthMeters = Settings.WorldDepthKilometers * 1000.0;
		Metrics.ElevationScaleMeters = Settings.ElevationScaleMeters;
		Metrics.SeaLevelMeters = 0.0;
		FEonformTerrainPhysicalContext::SetActive(Metrics);
	}

	FEonformTerrainGraphOutputSettings Settings;
	uint64 Revision = 1;
	uint64 PublishedAnalysisRevision = 0;
	bool bAnalysisPending = false;
	bool bAnalysisAvailable = false;
	FString AnalysisError;

	FEonformTerrainRecipe GenerationRecipe;
	FEonformTerrainEvaluationContext GenerationContext;
	uint64 GenerationGraphGeneration = 0;
	bool bGenerationPlanAvailable = false;
};
