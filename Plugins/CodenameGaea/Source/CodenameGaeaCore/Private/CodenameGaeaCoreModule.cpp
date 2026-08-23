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
#include "GaeaCryosphereNodes.h"
#include "GaeaDenoiseNode.h"
#include "GaeaDistanceNode.h"
#include "GaeaDotNoiseNode.h"
#include "GaeaDriftNoiseNode.h"
#include "GaeaEcologyNodes.h"
#include "GaeaErosionNode.h"
#include "GaeaFileNodeDecoder.h"
#include "GaeaFlipNode.h"
#include "GaeaFlowMapNodes.h"
#include "GaeaFractalTerracesNode.h"
#include "GaeaGaborNode.h"
#include "GaeaHeightNode.h"
#include "GaeaHemisphereNode.h"
#include "GaeaInvertNode.h"
#include "GaeaLakeNode.h"
#include "GaeaSeaNode.h"
#include "GaeaLinearGradientNode.h"
#include "GaeaModifyFoundationNodes.h"
#include "GaeaModifyProfileNodes.h"
#include "GaeaModifySpatialNodes.h"
#include "GaeaMultiCombineNode.h"
#include "GaeaNetworkProcessNodes.h"
#include "GaeaPerlinNode.h"
#include "GaeaPrimitiveAssetNodes.h"
#include "GaeaPrimitiveCoverageNodes.h"
#include "GaeaRadialGradientNode.h"
#include "GaeaRecurveNode.h"
#include "GaeaShaperNode.h"
#include "GaeaSharpenNode.h"
#include "GaeaSimulateEvolutionNodes.h"
#include "GaeaSimulateFoundationNodes.h"
#include "GaeaSineNode.h"
#include "GaeaSlopeNode.h"
#include "GaeaSoftClipNode.h"
#include "GaeaSurfaceNodes.h"
#include "GaeaTerraceNode.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainSemanticNodes.h"
#include "GaeaThermalErosionNode.h"
#include "GaeaThresholdNode.h"
#include "GaeaTransformNode.h"
#include "GaeaWizardNodes.h"
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
		RegisterGaeaSimulateFoundationNodes();
		RegisterGaeaSimulateEvolutionNodes();
		RegisterGaeaLakeNode();
		RegisterGaeaSeaNode();
		RegisterGaeaCryosphereNodes();
		RegisterGaeaSnowfieldNode();
		RegisterGaeaGlacierNode();
		RegisterGaeaNetworkProcessNodes();
		RegisterGaeaEcologyNodes();
		RegisterGaeaWizardNodes();
		RegisterGaeaCurvatureNode();
		RegisterGaeaHeightNode();
		RegisterGaeaAngleNode();
		RegisterGaeaSlopeNode();
		RegisterGaeaFlowMapNodes();
		RegisterGaeaTerrainSemanticNodes();
		RegisterGaeaCombineNode();
		RegisterGaeaClampNode();

		RegisterGaeaAdjustNode();
		RegisterGaeaApertureNode();
		RegisterGaeaAutoLevelNode();
		RegisterGaeaBlobRemoverNode();
		RegisterGaeaBlurNode();
		RegisterGaeaClipNode();
		RegisterGaeaCurveNode();
		RegisterGaeaDeflateNode();
		RegisterGaeaDenoiseNode();
		RegisterGaeaDilateNode();
		RegisterGaeaDirectionalWarpNode();
		RegisterGaeaDistanceNode();
		RegisterGaeaEqualizeNode();
		RegisterGaeaExtendNode();
		RegisterGaeaFilterNode();
		RegisterGaeaFlipNode();
		RegisterGaeaFoldNode();
		RegisterGaeaGraphicEQNode();
		RegisterGaeaHealNode();
		RegisterGaeaMatchNode();
		RegisterGaeaMedianNode();
		RegisterGaeaMeshifyNode();
		RegisterGaeaOrigamiNode();
		RegisterGaeaPixelateNode();
		RegisterGaeaRecurveNode();
		RegisterGaeaShaperNode();
		RegisterGaeaSharpenNode();
		RegisterGaeaSlopeBlurNode();
		RegisterGaeaSlopeWarpNode();
		RegisterGaeaSoftClipNode();
		RegisterGaeaSwirlNode();
		RegisterGaeaThermalShaperNode();
		RegisterGaeaThresholdNode();
		RegisterGaeaTransformNode();
		RegisterGaeaTransposeNode();
		RegisterGaeaVariableBlurNode();
		RegisterGaeaWarpNode();
		RegisterGaeaWhorlNode();

		RegisterGaeaInvertNode();
		RegisterGaeaMultiCombineNode();
		RegisterGaeaSineNode();
		RegisterGaeaZeroBordersNode();
		RegisterGaeaFractalTerracesNode();
		RegisterGaeaTerraceNode();
		RegisterGaeaSurfaceNodes();

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
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Lake, TEXT("Lake"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Sea, TEXT("Sea"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Snow, TEXT("Snow"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Snowfield, TEXT("Snowfield"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Glacier, TEXT("Glacier"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Anastomosis, TEXT("Anastomosis"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Lichtenberg, TEXT("Lichtenberg"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Trees, TEXT("Trees"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Shrubs, TEXT("Shrubs"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Wizard, TEXT("Wizard"), TEXT("Simulate"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Wizard2, TEXT("Wizard2"), TEXT("Simulate"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Adjust, TEXT("Adjust"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Aperture, TEXT("Aperture"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::AutoLevel, TEXT("Autolevel"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::BlobRemover, TEXT("BlobRemover"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Blur, TEXT("Blur"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Clamp, TEXT("Clamp"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Clip, TEXT("Clip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Curve, TEXT("Curve"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Deflate, TEXT("Deflate"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Denoise, TEXT("Denoise"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Dilate, TEXT("Dilate"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::DirectionalWarp, TEXT("DirectionalWarp"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Distance, TEXT("Distance"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Equalize, TEXT("Equalize"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Extend, TEXT("Extend"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Filter, TEXT("Filter"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Flip, TEXT("Flip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Fold, TEXT("Fold"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::GraphicEQ, TEXT("GraphicEQ"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Heal, TEXT("Heal"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Match, TEXT("Match"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Median, TEXT("Median"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Meshify, TEXT("Meshify"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Origami, TEXT("Origami"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Pixelate, TEXT("Pixelate"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Recurve, TEXT("Recurve"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Shaper, TEXT("Shaper"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Sharpen, TEXT("Sharpen"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::SlopeBlur, TEXT("SlopeBlur"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::SlopeWarp, TEXT("SlopeWarp"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::SoftClip, TEXT("SoftClip"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Swirl, TEXT("Swirl"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::ThermalShaper, TEXT("ThermalShaper"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Threshold, TEXT("Threshold"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Transform, TEXT("Transform"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Transpose, TEXT("Transpose"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::VariableBlur, TEXT("VariableBlur"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Warp, TEXT("Warp"), TEXT("Modify"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Whorl, TEXT("Whorl"), TEXT("Modify"));

		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::FractalTerraces, TEXT("FractalTerraces"), TEXT("Surface"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Terrace, TEXT("Terraces"), TEXT("Surface"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Angle, TEXT("Angle"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Curvature, TEXT("Curvature"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Height, TEXT("Height"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Slope, TEXT("Slope"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::FlowMap, TEXT("FlowMap"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::FlowMapClassic, TEXT("FlowMapClassic"), TEXT("Derive"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Combine, TEXT("Combine"), TEXT("Utility"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::ZeroBorders, TEXT("Edge"), TEXT("Utility"));
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Invert, TEXT("Invert"), TEXT("Legacy"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::Sine, TEXT("Sine"), TEXT("Legacy"), true);
		ApplyCurrentGaeaPublicMetadata(GaeaTerrainNodeTypes::MultiCombine, TEXT("MultiCombine"), TEXT("Legacy"), true);
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)