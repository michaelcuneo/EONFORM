#if WITH_DEV_AUTOMATION_TESTS

#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/AutomationTest.h"

namespace GaeaCurrentNodeContractTests
{
	bool CheckNode(FAutomationTestBase& Test, FName Type, const TCHAR* DisplayName, const TCHAR* Category, int32 ParameterCount, bool bHidden = false)
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		const FString Prefix = FString::Printf(TEXT("%s: "), DisplayName);
		if (!Test.TestTrue(*(Prefix + TEXT("descriptor exists")), FGaeaTerrainNodeDescriptorRegistry::Get(Type, Descriptor))) return false;
		Test.TestEqual(*(Prefix + TEXT("display name")), Descriptor.DisplayName, FString(DisplayName));
		Test.TestEqual(*(Prefix + TEXT("category")), Descriptor.Category, FString(Category));
		Test.TestEqual(*(Prefix + TEXT("parameter count")), Descriptor.Parameters.Num(), ParameterCount);
		Test.TestEqual(*(Prefix + TEXT("hidden state")), Descriptor.bHiddenInGraph, bHidden);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGaeaCurrentNodeContractTest, "CodenameGaea.Core.Graph.CurrentGaeaNodeContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGaeaCurrentNodeContractTest::RunTest(const FString& Parameters)
{
	using namespace GaeaCurrentNodeContractTests;
	CheckNode(*this, GaeaTerrainNodeTypes::PerlinNoise, TEXT("Perlin"), TEXT("Primitive"), 14);
	CheckNode(*this, GaeaTerrainNodeTypes::Cellular, TEXT("Cellular"), TEXT("Primitive"), 10);
	CheckNode(*this, GaeaTerrainNodeTypes::Cellular3D, TEXT("Cellular3D"), TEXT("Primitive"), 10);
	CheckNode(*this, GaeaTerrainNodeTypes::Cone, TEXT("Cone"), TEXT("Primitive"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::Constant, TEXT("Constant"), TEXT("Primitive"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::Cracks, TEXT("Cracks"), TEXT("Primitive"), 10);
	CheckNode(*this, GaeaTerrainNodeTypes::DotNoise, TEXT("DotNoise"), TEXT("Primitive"), 7);
	CheckNode(*this, GaeaTerrainNodeTypes::Draw, TEXT("Draw"), TEXT("Primitive"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::DriftNoise, TEXT("DriftNoise"), TEXT("Primitive"), 10);
	CheckNode(*this, GaeaTerrainNodeTypes::File, TEXT("File"), TEXT("Primitive"), 19);
	CheckNode(*this, GaeaTerrainNodeTypes::Gabor, TEXT("Gabor"), TEXT("Primitive"), 6);
	CheckNode(*this, GaeaTerrainNodeTypes::Hemisphere, TEXT("Hemisphere"), TEXT("Primitive"), 5);
	CheckNode(*this, GaeaTerrainNodeTypes::LinearGradient, TEXT("LinearGradient"), TEXT("Primitive"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::LineNoise, TEXT("LineNoise"), TEXT("Primitive"), 5);
	CheckNode(*this, GaeaTerrainNodeTypes::MultiFractal, TEXT("MultiFractal"), TEXT("Primitive"), 26);
	CheckNode(*this, GaeaTerrainNodeTypes::Noise, TEXT("Noise"), TEXT("Primitive"), 6);
	CheckNode(*this, GaeaTerrainNodeTypes::Object, TEXT("Object"), TEXT("Primitive"), 11);
	CheckNode(*this, GaeaTerrainNodeTypes::Pattern, TEXT("Pattern"), TEXT("Primitive"), 7);
	CheckNode(*this, GaeaTerrainNodeTypes::RadialGradient, TEXT("RadialGradient"), TEXT("Primitive"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::Shape, TEXT("Shape"), TEXT("Primitive"), 10);
	CheckNode(*this, GaeaTerrainNodeTypes::TileInput, TEXT("TileInput"), TEXT("Primitive"), 5);
	CheckNode(*this, GaeaTerrainNodeTypes::Voronoi, TEXT("Voronoi"), TEXT("Primitive"), 15);
	CheckNode(*this, GaeaTerrainNodeTypes::WaveShine, TEXT("WaveShine"), TEXT("Primitive"), 15);
	CheckNode(*this, GaeaTerrainNodeTypes::HydraulicErosion, TEXT("Erosion"), TEXT("Simulate"), 20);
	CheckNode(*this, GaeaTerrainNodeTypes::ThermalErosion, TEXT("Thermal"), TEXT("Simulate"), 11);

	CheckNode(*this, GaeaTerrainNodeTypes::Adjust, TEXT("Adjust"), TEXT("Modify"), 11);
	CheckNode(*this, GaeaTerrainNodeTypes::Aperture, TEXT("Aperture"), TEXT("Modify"), 7);
	CheckNode(*this, GaeaTerrainNodeTypes::AutoLevel, TEXT("Autolevel"), TEXT("Modify"), 0);
	CheckNode(*this, GaeaTerrainNodeTypes::BlobRemover, TEXT("BlobRemover"), TEXT("Modify"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::Blur, TEXT("Blur"), TEXT("Modify"), 1);
	CheckNode(*this, GaeaTerrainNodeTypes::Clamp, TEXT("Clamp"), TEXT("Modify"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::Clip, TEXT("Clip"), TEXT("Modify"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::Deflate, TEXT("Deflate"), TEXT("Modify"), 1);
	CheckNode(*this, GaeaTerrainNodeTypes::Denoise, TEXT("Denoise"), TEXT("Modify"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::Dilate, TEXT("Dilate"), TEXT("Modify"), 5);
	CheckNode(*this, GaeaTerrainNodeTypes::Distance, TEXT("Distance"), TEXT("Modify"), 13);
	CheckNode(*this, GaeaTerrainNodeTypes::Equalize, TEXT("Equalize"), TEXT("Modify"), 0);
	CheckNode(*this, GaeaTerrainNodeTypes::Extend, TEXT("Extend"), TEXT("Modify"), 1);
	CheckNode(*this, GaeaTerrainNodeTypes::Flip, TEXT("Flip"), TEXT("Modify"), 1);
	CheckNode(*this, GaeaTerrainNodeTypes::Median, TEXT("Median"), TEXT("Modify"), 2);
	CheckNode(*this, GaeaTerrainNodeTypes::Recurve, TEXT("Recurve"), TEXT("Modify"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::Shaper, TEXT("Shaper"), TEXT("Modify"), 5);
	CheckNode(*this, GaeaTerrainNodeTypes::Sharpen, TEXT("Sharpen"), TEXT("Modify"), 2);
	CheckNode(*this, GaeaTerrainNodeTypes::SoftClip, TEXT("SoftClip"), TEXT("Modify"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::Threshold, TEXT("Threshold"), TEXT("Modify"), 1);
	CheckNode(*this, GaeaTerrainNodeTypes::Transform, TEXT("Transform"), TEXT("Modify"), 11);

	CheckNode(*this, GaeaTerrainNodeTypes::FractalTerraces, TEXT("FractalTerraces"), TEXT("Surface"), 20);
	CheckNode(*this, GaeaTerrainNodeTypes::Terrace, TEXT("Terraces"), TEXT("Surface"), 5);
	CheckNode(*this, GaeaTerrainNodeTypes::Angle, TEXT("Angle"), TEXT("Derive"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::Curvature, TEXT("Curvature"), TEXT("Derive"), 3);
	CheckNode(*this, GaeaTerrainNodeTypes::Height, TEXT("Height"), TEXT("Derive"), 2);
	CheckNode(*this, GaeaTerrainNodeTypes::Slope, TEXT("Slope"), TEXT("Derive"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::Combine, TEXT("Combine"), TEXT("Utility"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::ZeroBorders, TEXT("Edge"), TEXT("Utility"), 4);
	CheckNode(*this, GaeaTerrainNodeTypes::SourceDataset, TEXT("Source Dataset"), TEXT("Internal"), 0, true);
	CheckNode(*this, GaeaTerrainNodeTypes::TerrainShape, TEXT("Terrain Shape"), TEXT("Internal"), 8, true);
	CheckNode(*this, GaeaTerrainNodeTypes::Invert, TEXT("Invert"), TEXT("Legacy"), 0, true);
	CheckNode(*this, GaeaTerrainNodeTypes::Sine, TEXT("Sine"), TEXT("Legacy"), 1, true);
	CheckNode(*this, GaeaTerrainNodeTypes::MultiCombine, TEXT("MultiCombine"), TEXT("Legacy"), 12, true);
	return true;
}

#endif
