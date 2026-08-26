#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace EonformCurrentNodeContractTests
{
	bool CheckNode(FAutomationTestBase& Test, FName Type, const TCHAR* DisplayName, const TCHAR* Category, int32 ParameterCount, bool bHidden = false)
	{
		FEonformTerrainNodeDescriptor Descriptor;
		const FString Prefix = FString::Printf(TEXT("%s: "), DisplayName);
		if (!Test.TestTrue(*(Prefix + TEXT("descriptor exists")), FEonformTerrainNodeDescriptorRegistry::Get(Type, Descriptor))) return false;
		Test.TestEqual(*(Prefix + TEXT("display name")), Descriptor.DisplayName, FString(DisplayName));
		Test.TestEqual(*(Prefix + TEXT("category")), Descriptor.Category, FString(Category));
		Test.TestEqual(*(Prefix + TEXT("parameter count")), Descriptor.Parameters.Num(), ParameterCount);
		Test.TestEqual(*(Prefix + TEXT("hidden state")), Descriptor.bHiddenInGraph, bHidden);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformCurrentNodeContractTest, "Eonform.Core.Graph.CurrentEonformNodeContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformCurrentNodeContractTest::RunTest(const FString& Parameters)
{
	using namespace EonformCurrentNodeContractTests;
	CheckNode(*this, EonformTerrainNodeTypes::PerlinNoise, TEXT("Perlin"), TEXT("Primitive"), 14);
	CheckNode(*this, EonformTerrainNodeTypes::Cellular, TEXT("Cellular"), TEXT("Primitive"), 10);
	CheckNode(*this, EonformTerrainNodeTypes::Cellular3D, TEXT("Cellular3D"), TEXT("Primitive"), 10);
	CheckNode(*this, EonformTerrainNodeTypes::Cone, TEXT("Cone"), TEXT("Primitive"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Constant, TEXT("Constant"), TEXT("Primitive"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Cracks, TEXT("Cracks"), TEXT("Primitive"), 10);
	CheckNode(*this, EonformTerrainNodeTypes::DotNoise, TEXT("DotNoise"), TEXT("Primitive"), 7);
	CheckNode(*this, EonformTerrainNodeTypes::Draw, TEXT("Draw"), TEXT("Primitive"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::DriftNoise, TEXT("DriftNoise"), TEXT("Primitive"), 10);
	CheckNode(*this, EonformTerrainNodeTypes::File, TEXT("File"), TEXT("Primitive"), 19);
	CheckNode(*this, EonformTerrainNodeTypes::Gabor, TEXT("Gabor"), TEXT("Primitive"), 6);
	CheckNode(*this, EonformTerrainNodeTypes::Hemisphere, TEXT("Hemisphere"), TEXT("Primitive"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::LinearGradient, TEXT("LinearGradient"), TEXT("Primitive"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::LineNoise, TEXT("LineNoise"), TEXT("Primitive"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::MultiFractal, TEXT("MultiFractal"), TEXT("Primitive"), 26);
	CheckNode(*this, EonformTerrainNodeTypes::Noise, TEXT("Noise"), TEXT("Primitive"), 6);
	CheckNode(*this, EonformTerrainNodeTypes::Object, TEXT("Object"), TEXT("Primitive"), 11);
	CheckNode(*this, EonformTerrainNodeTypes::Pattern, TEXT("Pattern"), TEXT("Primitive"), 7);
	CheckNode(*this, EonformTerrainNodeTypes::RadialGradient, TEXT("RadialGradient"), TEXT("Primitive"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Shape, TEXT("Shape"), TEXT("Primitive"), 10);
	CheckNode(*this, EonformTerrainNodeTypes::TileInput, TEXT("TileInput"), TEXT("Primitive"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::Voronoi, TEXT("Voronoi"), TEXT("Primitive"), 15);
	CheckNode(*this, EonformTerrainNodeTypes::WaveShine, TEXT("WaveShine"), TEXT("Primitive"), 15);
	CheckNode(*this, EonformTerrainNodeTypes::HydraulicErosion, TEXT("Erosion"), TEXT("Simulate"), 20);
	CheckNode(*this, EonformTerrainNodeTypes::ThermalErosion, TEXT("Thermal"), TEXT("Simulate"), 11);
	CheckNode(*this, EonformTerrainNodeTypes::Lake, TEXT("Lake"), TEXT("Simulate"), 6);
	CheckNode(*this, EonformTerrainNodeTypes::Sea, TEXT("Sea"), TEXT("Simulate"), 5);

	CheckNode(*this, EonformTerrainNodeTypes::Adjust, TEXT("Adjust"), TEXT("Modify"), 11);
	CheckNode(*this, EonformTerrainNodeTypes::Aperture, TEXT("Aperture"), TEXT("Modify"), 7);
	CheckNode(*this, EonformTerrainNodeTypes::AutoLevel, TEXT("Autolevel"), TEXT("Modify"), 0);
	CheckNode(*this, EonformTerrainNodeTypes::BlobRemover, TEXT("BlobRemover"), TEXT("Modify"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Blur, TEXT("Blur"), TEXT("Modify"), 1);
	CheckNode(*this, EonformTerrainNodeTypes::Clamp, TEXT("Clamp"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Clip, TEXT("Clip"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Curve, TEXT("Curve"), TEXT("Modify"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Deflate, TEXT("Deflate"), TEXT("Modify"), 1);
	CheckNode(*this, EonformTerrainNodeTypes::Denoise, TEXT("Denoise"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Dilate, TEXT("Dilate"), TEXT("Modify"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::DirectionalWarp, TEXT("DirectionalWarp"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Distance, TEXT("Distance"), TEXT("Modify"), 13);
	CheckNode(*this, EonformTerrainNodeTypes::Equalize, TEXT("Equalize"), TEXT("Modify"), 0);
	CheckNode(*this, EonformTerrainNodeTypes::Extend, TEXT("Extend"), TEXT("Modify"), 1);
	CheckNode(*this, EonformTerrainNodeTypes::Filter, TEXT("Filter"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Flip, TEXT("Flip"), TEXT("Modify"), 1);
	CheckNode(*this, EonformTerrainNodeTypes::Fold, TEXT("Fold"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::GraphicEQ, TEXT("GraphicEQ"), TEXT("Modify"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::Heal, TEXT("Heal"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Match, TEXT("Match"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Median, TEXT("Median"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::Meshify, TEXT("Meshify"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::Origami, TEXT("Origami"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::Pixelate, TEXT("Pixelate"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::Recurve, TEXT("Recurve"), TEXT("Modify"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Shaper, TEXT("Shaper"), TEXT("Modify"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::Sharpen, TEXT("Sharpen"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::SlopeBlur, TEXT("SlopeBlur"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::SlopeWarp, TEXT("SlopeWarp"), TEXT("Modify"), 6);
	CheckNode(*this, EonformTerrainNodeTypes::SoftClip, TEXT("SoftClip"), TEXT("Modify"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Swirl, TEXT("Swirl"), TEXT("Modify"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::ThermalShaper, TEXT("ThermalShaper"), TEXT("Modify"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Threshold, TEXT("Threshold"), TEXT("Modify"), 1);
	CheckNode(*this, EonformTerrainNodeTypes::Transform, TEXT("Transform"), TEXT("Modify"), 11);
	CheckNode(*this, EonformTerrainNodeTypes::Transpose, TEXT("Transpose"), TEXT("Modify"), 6);
	CheckNode(*this, EonformTerrainNodeTypes::VariableBlur, TEXT("VariableBlur"), TEXT("Modify"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::Warp, TEXT("Warp"), TEXT("Modify"), 14);
	CheckNode(*this, EonformTerrainNodeTypes::Whorl, TEXT("Whorl"), TEXT("Modify"), 4);

	CheckNode(*this, EonformTerrainNodeTypes::FractalTerraces, TEXT("FractalTerraces"), TEXT("Surface"), 20);
	CheckNode(*this, EonformTerrainNodeTypes::Terrace, TEXT("Terraces"), TEXT("Surface"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::Angle, TEXT("Angle"), TEXT("Derive"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Curvature, TEXT("Curvature"), TEXT("Derive"), 3);
	CheckNode(*this, EonformTerrainNodeTypes::Height, TEXT("Height"), TEXT("Derive"), 2);
	CheckNode(*this, EonformTerrainNodeTypes::Slope, TEXT("Slope"), TEXT("Derive"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::Combine, TEXT("Combine"), TEXT("Utility"), 5);
	CheckNode(*this, EonformTerrainNodeTypes::ZeroBorders, TEXT("Edge"), TEXT("Utility"), 4);
	CheckNode(*this, EonformTerrainNodeTypes::SourceDataset, TEXT("Source Dataset"), TEXT("Internal"), 0, true);
	CheckNode(*this, EonformTerrainNodeTypes::TerrainShape, TEXT("Terrain Shape"), TEXT("Internal"), 8, true);
	CheckNode(*this, EonformTerrainNodeTypes::Invert, TEXT("Invert"), TEXT("Legacy"), 0, true);
	CheckNode(*this, EonformTerrainNodeTypes::Sine, TEXT("Sine"), TEXT("Legacy"), 1, true);
	CheckNode(*this, EonformTerrainNodeTypes::MultiCombine, TEXT("MultiCombine"), TEXT("Legacy"), 12, true);
	return true;
}

#endif