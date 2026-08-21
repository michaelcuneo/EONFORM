#include "Modules/ModuleManager.h"

#include "GaeaAngleNode.h"
#include "GaeaAutoLevelNode.h"
#include "GaeaBlurNode.h"
#include "GaeaClampNode.h"
#include "GaeaCombineNode.h"
#include "GaeaCurvatureNode.h"
#include "GaeaDenoiseNode.h"
#include "GaeaDistanceNode.h"
#include "GaeaFlipNode.h"
#include "GaeaFractalTerracesNode.h"
#include "GaeaHeightNode.h"
#include "GaeaInvertNode.h"
#include "GaeaMultiCombineNode.h"
#include "GaeaRecurveNode.h"
#include "GaeaShaperNode.h"
#include "GaeaSharpenNode.h"
#include "GaeaSineNode.h"
#include "GaeaSlopeNode.h"
#include "GaeaSoftClipNode.h"
#include "GaeaTerraceNode.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosionNode.h"
#include "GaeaThresholdNode.h"
#include "GaeaTransformNode.h"
#include "GaeaZeroBordersNode.h"

namespace
{
	void ApplyCurrentGaeaPublicMetadata(FName Type, const TCHAR* DisplayName, const TCHAR* Category, bool bHidden = false)
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		if (!FGaeaTerrainNodeDescriptorRegistry::Get(Type, Descriptor)) return;
		Descriptor.DisplayName = DisplayName;
		Descriptor.Category = Category;
		Descriptor.bHiddenInGraph = bHidden;
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	}
}

class FCodenameGaeaCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGaeaTerrainNodeDescriptorRegistry::RegisterBuiltIns();

		RegisterGaeaThermalErosionNode();
		RegisterGaeaCurvatureNode();
		RegisterGaeaHeightNode();
		RegisterGaeaAngleNode();
		RegisterGaeaSlopeNode();
		RegisterGaeaCombineNode();
		RegisterGaeaClampNode();
		RegisterGaeaAutoLevelNode();
		RegisterGaeaBlurNode();
		RegisterGaeaDenoiseNode();
		RegisterGaeaFlipNode();
		RegisterGaeaInvertNode();
		RegisterGaeaMultiCombineNode();
		RegisterGaeaSharpenNode();
		RegisterGaeaSineNode();
		RegisterGaeaThresholdNode();
		RegisterGaeaTransformNode();
		RegisterGaeaZeroBordersNode();
		RegisterGaeaDistanceNode();
		RegisterGaeaFractalTerracesNode();
		RegisterGaeaRecurveNode();
		RegisterGaeaShaperNode();
		RegisterGaeaSoftClipNode();
		RegisterGaeaTerraceNode();

		// Keep the public graph menu aligned with the current Gaea 2.x Node Map.
		// Integration-only and superseded Gaea 1 nodes remain registered for the
		// runtime but are not presented as authorable Gaea nodes.
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::SourceDataset, TEXT("Source Dataset"), TEXT("Internal"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::TerrainShape, TEXT("Terrain Shape"), TEXT("Internal"), true);

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::PerlinNoise, TEXT("Perlin"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::HydraulicErosion, TEXT("Erosion"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::ThermalErosion, TEXT("Thermal"), TEXT("Simulate"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::AutoLevel, TEXT("Autolevel"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Blur, TEXT("Blur"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Clamp, TEXT("Clamp"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Denoise, TEXT("Denoise"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Distance, TEXT("Distance"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Flip, TEXT("Flip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Recurve, TEXT("Recurve"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Shaper, TEXT("Shaper"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Sharpen, TEXT("Sharpen"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::SoftClip, TEXT("SoftClip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Threshold, TEXT("Threshold"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Transform, TEXT("Transform"), TEXT("Modify"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::FractalTerraces, TEXT("FractalTerraces"), TEXT("Surface"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Terrace, TEXT("Terraces"), TEXT("Surface"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Angle, TEXT("Angle"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Curvature, TEXT("Curvature"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Height, TEXT("Height"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Slope, TEXT("Slope"), TEXT("Derive"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Combine, TEXT("Combine"), TEXT("Utility"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::ZeroBorders, TEXT("Edge"), TEXT("Utility"));

		// These old public nodes do not exist in the current Gaea Node Map. Keep
		// their evaluators available for existing recipes while removing them
		// from the authoring surface. MultiCombine will return as current Layers
		// once its current stack contract is implemented rather than relabelled.
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Invert, TEXT("Invert"), TEXT("Legacy"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Sine, TEXT("Sine"), TEXT("Legacy"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::MultiCombine, TEXT("MultiCombine"), TEXT("Legacy"), true);
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
