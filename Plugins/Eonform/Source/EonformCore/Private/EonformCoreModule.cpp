#include "Modules/ModuleManager.h"

#include "EonformAngleNode.h"
#include "EonformAutoLevelNode.h"
#include "EonformBlurNode.h"
#include "EonformCellular3DNode.h"
#include "EonformCellularNode.h"
#include "EonformClampNode.h"
#include "EonformCombineNode.h"
#include "EonformConeNode.h"
#include "EonformConstantNode.h"
#include "EonformCracksNode.h"
#include "EonformCurvatureNode.h"
#include "EonformCryosphereNodes.h"
#include "EonformDenoiseNode.h"
#include "EonformDistanceNode.h"
#include "EonformDotNoiseNode.h"
#include "EonformDriftNoiseNode.h"
#include "EonformEcologyNodes.h"
#include "EonformErosionNode.h"
#include "EonformFileNodeDecoder.h"
#include "EonformFlipNode.h"
#include "EonformFlowMapNodes.h"
#include "EonformFractalTerracesNode.h"
#include "EonformGaborNode.h"
#include "EonformHeightNode.h"
#include "EonformHemisphereNode.h"
#include "EonformInvertNode.h"
#include "EonformLakeNode.h"
#include "EonformSeaNode.h"
#include "EonformLinearGradientNode.h"
#include "EonformModifyFoundationNodes.h"
#include "EonformModifyProfileNodes.h"
#include "EonformModifySpatialNodes.h"
#include "EonformMultiCombineNode.h"
#include "EonformNetworkProcessNodes.h"
#include "EonformPerlinNode.h"
#include "EonformPrimitiveAssetNodes.h"
#include "EonformPrimitiveCoverageNodes.h"
#include "EonformRadialGradientNode.h"
#include "EonformRecurveNode.h"
#include "EonformReferenceFidelityNodes.h"
#include "EonformReferenceFidelityExtendedNodes.h"
#include "EonformReferenceFidelityProcessNodes.h"
#include "EonformReferenceFidelityTransposeNode.h"
#include "EonformRidgeNode.h"
#include "EonformShaperNode.h"
#include "EonformSharpenNode.h"
#include "EonformSimulateEvolutionNodes.h"
#include "EonformSimulateFoundationNodes.h"
#include "EonformSineNode.h"
#include "EonformSlopeNode.h"
#include "EonformSoftClipNode.h"
#include "EonformSurfaceAnalysisNodes.h"
#include "EonformSurfaceNodes.h"
#include "EonformTerraceNode.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainSemanticNodes.h"
#include "EonformThermalErosionNode.h"
#include "EonformThresholdNode.h"
#include "EonformTransformNode.h"
#include "EonformUtilityNodes.h"
#include "EonformWizardNodes.h"
#include "EonformZeroBordersNode.h"

namespace
{
	void ApplyCurrentEonformPublicMetadata(FName Type, const TCHAR* DisplayName, const TCHAR* Category, bool bHidden = false)
	{
		FEonformTerrainNodeDescriptor Descriptor;
		if (!FEonformTerrainNodeDescriptorRegistry::Get(Type, Descriptor)) return;
		Descriptor.DisplayName = DisplayName;
		Descriptor.Category = Category;
		Descriptor.bHiddenInGraph = bHidden;
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	}
}

class FEonformCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FEonformTerrainNodeDescriptorRegistry::RegisterBuiltIns();

		RegisterEonformPerlinNode();
		RegisterEonformCellularNode();
		RegisterEonformCellular3DNode();
		RegisterEonformConeNode();
		RegisterEonformConstantNode();
		RegisterEonformCracksNode();
		RegisterEonformDotNoiseNode();
		RegisterEonformDrawNode();
		RegisterEonformDriftNoiseNode();
		RegisterEonformFileNode();
		RegisterEonformFileNodeDecoder();
		RegisterEonformGaborNode();
		RegisterEonformHemisphereNode();
		RegisterEonformLinearGradientNode();
		RegisterEonformLineNoiseNode();
		RegisterEonformMultiFractalNode();
		RegisterEonformNoiseNode();
		RegisterEonformObjectNode();
		RegisterEonformPatternNode();
		RegisterEonformRadialGradientNode();
		RegisterEonformShapeNode();
		RegisterEonformTileInputNode();
		RegisterEonformVoronoiNode();
		RegisterEonformWaveShineNode();
		RegisterEonformRidgeNode();
		RegisterEonformErosionNode();
		RegisterEonformThermalErosionNode();
		RegisterEonformSimulateFoundationNodes();
		RegisterEonformSimulateEvolutionNodes();
		RegisterEonformLakeNode();
		RegisterEonformSeaNode();
		RegisterEonformCryosphereNodes();
		RegisterEonformSnowfieldNode();
		RegisterEonformGlacierNode();
		RegisterEonformNetworkProcessNodes();
		RegisterEonformEcologyNodes();
		RegisterEonformWizardNodes();
		RegisterEonformCurvatureNode();
		RegisterEonformHeightNode();
		RegisterEonformAngleNode();
		RegisterEonformSlopeNode();
		RegisterEonformFlowMapNodes();
		RegisterEonformTerrainSemanticNodes();
		RegisterEonformSurfaceAnalysisNodes();
		RegisterEonformCombineNode();
		RegisterEonformClampNode();

		RegisterEonformAdjustNode();
		RegisterEonformApertureNode();
		RegisterEonformAutoLevelNode();
		RegisterEonformBlobRemoverNode();
		RegisterEonformBlurNode();
		RegisterEonformClipNode();
		RegisterEonformCurveNode();
		RegisterEonformDeflateNode();
		RegisterEonformDenoiseNode();
		RegisterEonformDilateNode();
		RegisterEonformDirectionalWarpNode();
		RegisterEonformDistanceNode();
		RegisterEonformEqualizeNode();
		RegisterEonformExtendNode();
		RegisterEonformFilterNode();
		RegisterEonformFlipNode();
		RegisterEonformFoldNode();
		RegisterEonformGraphicEQNode();
		RegisterEonformHealNode();
		RegisterEonformMatchNode();
		RegisterEonformMedianNode();
		RegisterEonformMeshifyNode();
		RegisterEonformOrigamiNode();
		RegisterEonformPixelateNode();
		RegisterEonformRecurveNode();
		RegisterEonformShaperNode();
		RegisterEonformSharpenNode();
		RegisterEonformSlopeBlurNode();
		RegisterEonformSlopeWarpNode();
		RegisterEonformSoftClipNode();
		RegisterEonformSwirlNode();
		RegisterEonformThermalShaperNode();
		RegisterEonformThresholdNode();
		RegisterEonformTransformNode();
		RegisterEonformTransposeNode();
		RegisterEonformVariableBlurNode();
		RegisterEonformWarpNode();
		RegisterEonformWhorlNode();

		RegisterEonformInvertNode();
		RegisterEonformMultiCombineNode();
		RegisterEonformSineNode();
		RegisterEonformZeroBordersNode();
		RegisterEonformFractalTerracesNode();
		RegisterEonformTerraceNode();
		RegisterEonformSurfaceNodes();
		RegisterEonformUtilityNodes();

		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::SourceDataset, TEXT("Source Dataset"), TEXT("Internal"), true);
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::TerrainShape, TEXT("Terrain Shape"), TEXT("Internal"), true);

		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::PerlinNoise, TEXT("Perlin"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Cellular, TEXT("Cellular"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Cellular3D, TEXT("Cellular3D"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Cone, TEXT("Cone"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Constant, TEXT("Constant"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Cracks, TEXT("Cracks"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::DotNoise, TEXT("DotNoise"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Draw, TEXT("Draw"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::DriftNoise, TEXT("DriftNoise"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::File, TEXT("File"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Gabor, TEXT("Gabor"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Hemisphere, TEXT("Hemisphere"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::LinearGradient, TEXT("LinearGradient"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::LineNoise, TEXT("LineNoise"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::MultiFractal, TEXT("MultiFractal"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Noise, TEXT("Noise"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Object, TEXT("Object"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Pattern, TEXT("Pattern"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::RadialGradient, TEXT("RadialGradient"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Shape, TEXT("Shape"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::TileInput, TEXT("TileInput"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Voronoi, TEXT("Voronoi"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::WaveShine, TEXT("WaveShine"), TEXT("Primitive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Ridge, TEXT("Ridge"), TEXT("Terrain"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::HydraulicErosion, TEXT("Erosion"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::ThermalErosion, TEXT("Thermal"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Lake, TEXT("Lake"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Sea, TEXT("Sea"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Snow, TEXT("Snow"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Snowfield, TEXT("Snowfield"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Glacier, TEXT("Glacier"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Anastomosis, TEXT("Anastomosis"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Lichtenberg, TEXT("Lichtenberg"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Trees, TEXT("Trees"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Shrubs, TEXT("Shrubs"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Wizard, TEXT("Wizard"), TEXT("Simulate"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Wizard2, TEXT("Wizard2"), TEXT("Simulate"));

		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Adjust, TEXT("Adjust"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Aperture, TEXT("Aperture"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::AutoLevel, TEXT("Autolevel"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::BlobRemover, TEXT("BlobRemover"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Blur, TEXT("Blur"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Clamp, TEXT("Clamp"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Clip, TEXT("Clip"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Curve, TEXT("Curve"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Deflate, TEXT("Deflate"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Denoise, TEXT("Denoise"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Dilate, TEXT("Dilate"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::DirectionalWarp, TEXT("DirectionalWarp"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Distance, TEXT("Distance"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Equalize, TEXT("Equalize"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Extend, TEXT("Extend"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Filter, TEXT("Filter"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Flip, TEXT("Flip"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Fold, TEXT("Fold"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::GraphicEQ, TEXT("GraphicEQ"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Heal, TEXT("Heal"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Match, TEXT("Match"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Median, TEXT("Median"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Meshify, TEXT("Meshify"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Origami, TEXT("Origami"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Pixelate, TEXT("Pixelate"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Recurve, TEXT("Recurve"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Shaper, TEXT("Shaper"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Sharpen, TEXT("Sharpen"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::SlopeBlur, TEXT("SlopeBlur"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::SlopeWarp, TEXT("SlopeWarp"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::SoftClip, TEXT("SoftClip"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Swirl, TEXT("Swirl"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::ThermalShaper, TEXT("ThermalShaper"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Threshold, TEXT("Threshold"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Transform, TEXT("Transform"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Transpose, TEXT("Transpose"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::VariableBlur, TEXT("VariableBlur"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Warp, TEXT("Warp"), TEXT("Modify"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Whorl, TEXT("Whorl"), TEXT("Modify"));

		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::FractalTerraces, TEXT("FractalTerraces"), TEXT("Surface"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Terrace, TEXT("Terraces"), TEXT("Surface"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Angle, TEXT("Angle"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Curvature, TEXT("Curvature"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Height, TEXT("Height"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Slope, TEXT("Slope"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::FlowMap, TEXT("FlowMap"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::FlowMapClassic, TEXT("FlowMapClassic"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Peaks, TEXT("Peaks"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::RockMap, TEXT("RockMap"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Soil, TEXT("Soil"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Normals, TEXT("Normals"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Occlusion, TEXT("Occlusion"), TEXT("Derive"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Combine, TEXT("Combine"), TEXT("Utility"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::ZeroBorders, TEXT("Edge"), TEXT("Utility"));
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Invert, TEXT("Invert"), TEXT("Legacy"), true);
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::Sine, TEXT("Sine"), TEXT("Legacy"), true);
		ApplyCurrentEonformPublicMetadata(EonformTerrainNodeTypes::MultiCombine, TEXT("MultiCombine"), TEXT("Legacy"), true);

		// Audited Gaea-facing implementations always register last. This makes the
		// Core module the authoritative registration order and prevents legacy
		// compatibility implementations from silently replacing corrected behavior.
		RegisterEonformReferenceFidelityNodes();
		RegisterEonformReferenceFidelityExtendedNodes();
		RegisterEonformReferenceFidelityTransposeNode();
		RegisterEonformReferenceFidelityProcessNodes();

		// Ridge is an authored terrain primitive/landform in its own right. Reapply
		// its registration after the audited families so no compatibility pass can
		// replace the shared Ridge evaluator used by compound landforms.
		RegisterEonformRidgeNode();
	}
};

IMPLEMENT_MODULE(FEonformCoreModule, EonformCore)