#include "Modules/ModuleManager.h"

#include "GaeaAngleNode.h"
#include "GaeaAutoLevelNode.h"
#include "GaeaBlurNode.h"
#include "GaeaCellular3DNode.h"
#include "GaeaCellularNode.h"
#include "GaeaClampNode.h"
#include "GaeaCombineNode.h"
#include "GaeaConeNode.h"
#include "GaeaConstantNode.h"
#include "GaeaCracksNode.h"
#include "GaeaCurvatureNode.h"
#include "GaeaDenoiseNode.h"
#include "GaeaDistanceNode.h"
#include "GaeaDotNoiseNode.h"
#include "GaeaDriftNoiseNode.h"
#include "GaeaErosionNode.h"
#include "GaeaFileNodeDecoder.h"
#include "GaeaFlipNode.h"
#include "GaeaFractalTerracesNode.h"
#include "GaeaGaborNode.h"
#include "GaeaHeightNode.h"
#include "GaeaHemisphereNode.h"
#include "GaeaInvertNode.h"
#include "GaeaLinearGradientNode.h"
#include "GaeaModifyFoundationNodes.h"
#include "GaeaMultiCombineNode.h"
#include "GaeaPerlinNode.h"
#include "GaeaPrimitiveAssetNodes.h"
#include "GaeaPrimitiveCoverageNodes.h"
#include "GaeaRadialGradientNode.h"
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

		RegisterGaeaPerlinNode();
		RegisterGaeaCellularNode();
		RegisterGaeaCellular3DNode();
		RegisterGaeaConeNode();
		RegisterGaeaConstantNode();
		RegisterGaeaCracksNode();
		RegisterGaeaDotNoiseNode();
		RegisterGaeaDrawNode();
		RegisterGaeaDriftNoiseNode();
		RegisterGaeaFileNode();
		RegisterGaeaFileNodeDecoder();
		RegisterGaeaGaborNode();
		RegisterGaeaHemisphereNode();
		RegisterGaeaLinearGradientNode();
		RegisterGaeaLineNoiseNode();
		RegisterGaeaMultiFractalNode();
		RegisterGaeaNoiseNode();
		RegisterGaeaObjectNode();
		RegisterGaeaPatternNode();
		RegisterGaeaRadialGradientNode();
		RegisterGaeaShapeNode();
		RegisterGaeaTileInputNode();
		RegisterGaeaVoronoiNode();
		RegisterGaeaWaveShineNode();
		RegisterGaeaErosionNode();
		RegisterGaeaThermalErosionNode();
		RegisterGaeaCurvatureNode();
		RegisterGaeaHeightNode();
		RegisterGaeaAngleNode();
		RegisterGaeaSlopeNode();
		RegisterGaeaCombineNode();
		RegisterGaeaClampNode();

		RegisterGaeaAdjustNode();
		RegisterGaeaApertureNode();
		RegisterGaeaAutoLevelNode();
		RegisterGaeaBlobRemoverNode();
		RegisterGaeaBlurNode();
		RegisterGaeaClipNode();
		RegisterGaeaDeflateNode();
		RegisterGaeaDenoiseNode();
		RegisterGaeaDilateNode();
		RegisterGaeaDistanceNode();
		RegisterGaeaEqualizeNode();
		RegisterGaeaExtendNode();
		RegisterGaeaFlipNode();
		RegisterGaeaMedianNode();
		RegisterGaeaRecurveNode();
		RegisterGaeaShaperNode();
		RegisterGaeaSharpenNode();
		RegisterGaeaSoftClipNode();
		RegisterGaeaThresholdNode();
		RegisterGaeaTransformNode();

		RegisterGaeaInvertNode();
		RegisterGaeaMultiCombineNode();
		RegisterGaeaSineNode();
		RegisterGaeaZeroBordersNode();
		RegisterGaeaFractalTerracesNode();
		RegisterGaeaTerraceNode();

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::SourceDataset, TEXT("Source Dataset"), TEXT("Internal"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::TerrainShape, TEXT("Terrain Shape"), TEXT("Internal"), true);

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::PerlinNoise, TEXT("Perlin"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Cellular, TEXT("Cellular"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Cellular3D, TEXT("Cellular3D"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Cone, TEXT("Cone"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Constant, TEXT("Constant"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Cracks, TEXT("Cracks"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::DotNoise, TEXT("DotNoise"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Draw, TEXT("Draw"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::DriftNoise, TEXT("DriftNoise"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::File, TEXT("File"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Gabor, TEXT("Gabor"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Hemisphere, TEXT("Hemisphere"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::LinearGradient, TEXT("LinearGradient"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::LineNoise, TEXT("LineNoise"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::MultiFractal, TEXT("MultiFractal"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Noise, TEXT("Noise"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Object, TEXT("Object"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Pattern, TEXT("Pattern"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::RadialGradient, TEXT("RadialGradient"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Shape, TEXT("Shape"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::TileInput, TEXT("TileInput"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Voronoi, TEXT("Voronoi"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::WaveShine, TEXT("WaveShine"), TEXT("Primitive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::HydraulicErosion, TEXT("Erosion"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::ThermalErosion, TEXT("Thermal"), TEXT("Simulate"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Adjust, TEXT("Adjust"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Aperture, TEXT("Aperture"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::AutoLevel, TEXT("Autolevel"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::BlobRemover, TEXT("BlobRemover"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Blur, TEXT("Blur"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Clamp, TEXT("Clamp"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Clip, TEXT("Clip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Deflate, TEXT("Deflate"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Denoise, TEXT("Denoise"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Dilate, TEXT("Dilate"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Distance, TEXT("Distance"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Equalize, TEXT("Equalize"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Extend, TEXT("Extend"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Flip, TEXT("Flip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Median, TEXT("Median"), TEXT("Modify"));
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

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Invert, TEXT("Invert"), TEXT("Legacy"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Sine, TEXT("Sine"), TEXT("Legacy"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::MultiCombine, TEXT("MultiCombine"), TEXT("Legacy"), true);
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
