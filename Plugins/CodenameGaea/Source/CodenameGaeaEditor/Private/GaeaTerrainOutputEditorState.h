#pragma once

#include "CoreMinimal.h"
#include "GaeaTerrainGraphAsset.h"

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
		++Revision;
	}

	void Reset()
	{
		Settings = FGaeaTerrainGraphOutputSettings();
		++Revision;
	}

	void SetWorldWidthKilometers(double Value) { Settings.WorldWidthKilometers = FMath::Max(Value, 0.001); ++Revision; }
	void SetWorldDepthKilometers(double Value) { Settings.WorldDepthKilometers = FMath::Max(Value, 0.001); ++Revision; }
	void SetElevationScaleMeters(double Value) { Settings.ElevationScaleMeters = FMath::Max(Value, 0.001); ++Revision; }
	void SetOutputResolution(int32 Value) { Settings.OutputResolution = FMath::Max(Value, 0); ++Revision; }
	void SetSectionLayout(EGaeaTerrainOutputSectionLayout Value) { Settings.SectionLayout = Value; ++Revision; }
	void SetSectionComplexity(EGaeaTerrainOutputComplexity Value) { Settings.SectionComplexity = Value; ++Revision; }
	void SetSectionsX(int32 Value) { Settings.SectionsX = FMath::Max(Value, 1); ++Revision; }
	void SetSectionsY(int32 Value) { Settings.SectionsY = FMath::Max(Value, 1); ++Revision; }
	void SetMeshPartitionDefinition(const FSoftObjectPath& Value) { Settings.MeshPartitionDefinition = Value; ++Revision; }

	uint64 GetRevision() const { return Revision; }

private:
	void Sanitize()
	{
		Settings.WorldWidthKilometers = FMath::Max(Settings.WorldWidthKilometers, 0.001);
		Settings.WorldDepthKilometers = FMath::Max(Settings.WorldDepthKilometers, 0.001);
		Settings.ElevationScaleMeters = FMath::Max(Settings.ElevationScaleMeters, 0.001);
		Settings.OutputResolution = FMath::Max(Settings.OutputResolution, 0);
		Settings.SectionsX = FMath::Max(Settings.SectionsX, 1);
		Settings.SectionsY = FMath::Max(Settings.SectionsY, 1);
	}

	FGaeaTerrainGraphOutputSettings Settings;
	uint64 Revision = 1;
};
