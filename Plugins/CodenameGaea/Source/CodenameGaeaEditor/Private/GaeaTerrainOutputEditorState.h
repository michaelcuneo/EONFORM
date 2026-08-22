#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainGraphAsset.h"
#include "GaeaTerrainPhysicalMetrics.h"

/** Shared editor-session state for the Terrain Output pane and the active graph asset. */
class FGaeaTerrainOutputEditorState
{
public:
	static FGaeaTerrainOutputEditorState& Get()
	{
		static FGaeaTerrainOutputEditorState State;
		return State;
	}

	const FGaeaTerrainGraphOutputSettings& GetSettings() const
	{
		return Settings;
	}

	void Load(const FGaeaTerrainGraphOutputSettings& InSettings)
	{
		Settings = InSettings;
		Sanitize();
		PublishPhysicalContext();
		++Revision;
	}

	void Reset()
	{
		Settings = FGaeaTerrainGraphOutputSettings();
		PublishPhysicalContext();
		++Revision;
	}

	void SetWorldWidthKilometers(double Value) { Settings.WorldWidthKilometers = FMath::Max(Value, 0.001); PublishPhysicalContext(); ++Revision; }
	void SetWorldDepthKilometers(double Value) { Settings.WorldDepthKilometers = FMath::Max(Value, 0.001); PublishPhysicalContext(); ++Revision; }
	void SetElevationScaleMeters(double Value) { Settings.ElevationScaleMeters = FMath::Max(Value, 0.001); PublishPhysicalContext(); ++Revision; }
	void SetOutputResolution(int32 Value) { Settings.OutputResolution = FMath::Max(Value, 0); ++Revision; }
	void SetSectionLayout(EGaeaTerrainOutputSectionLayout Value) { Settings.SectionLayout = Value; ++Revision; }
	void SetSectionComplexity(EGaeaTerrainOutputComplexity Value) { Settings.SectionComplexity = Value; ++Revision; }
	void SetSectionsX(int32 Value) { Settings.SectionsX = FMath::Max(Value, 1); ++Revision; }
	void SetSectionsY(int32 Value) { Settings.SectionsY = FMath::Max(Value, 1); ++Revision; }
	void SetMeshPartitionDefinition(const FSoftObjectPath& Value) { Settings.MeshPartitionDefinition = Value; ++Revision; }

	uint64 GetRevision() const { return Revision; }

	/**
	 * Automatic graph evaluation is asynchronous. Terrain Output uses this state
	 * to distinguish a genuinely missing result from a result that is still being
	 * calculated, and to prevent Generate Terrain from consuming an older snapshot.
	 */
	void BeginAnalysis()
	{
		bAnalysisPending = true;
		AnalysisError.Reset();
	}

	void CompleteAnalysis(uint64 InPublishedRevision)
	{
		bAnalysisPending = false;
		bAnalysisAvailable = InPublishedRevision != 0;
		PublishedAnalysisRevision = InPublishedRevision;
		AnalysisError.Reset();
	}

	void FailAnalysis(const FString& InError)
	{
		bAnalysisPending = false;
		bAnalysisAvailable = false;
		PublishedAnalysisRevision = 0;
		AnalysisError = InError;
	}

	void InvalidateAnalysis()
	{
		bAnalysisPending = false;
		bAnalysisAvailable = false;
		PublishedAnalysisRevision = 0;
		AnalysisError.Reset();
	}

	bool IsAnalysisPending() const { return bAnalysisPending; }
	bool IsAnalysisAvailable() const { return bAnalysisAvailable; }
	uint64 GetPublishedAnalysisRevision() const { return PublishedAnalysisRevision; }
	const FString& GetAnalysisError() const { return AnalysisError; }

private:
	FGaeaTerrainOutputEditorState()
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
		FGaeaTerrainPhysicalMetrics Metrics;
		Metrics.WorldWidthMeters = Settings.WorldWidthKilometers * 1000.0;
		Metrics.WorldDepthMeters = Settings.WorldDepthKilometers * 1000.0;
		Metrics.ElevationScaleMeters = Settings.ElevationScaleMeters;
		Metrics.SeaLevelMeters = 0.0;
		FGaeaTerrainPhysicalContext::SetActive(Metrics);
	}

	FGaeaTerrainGraphOutputSettings Settings;
	uint64 Revision = 1;
	uint64 PublishedAnalysisRevision = 0;
	bool bAnalysisPending = false;
	bool bAnalysisAvailable = false;
	FString AnalysisError;
};
