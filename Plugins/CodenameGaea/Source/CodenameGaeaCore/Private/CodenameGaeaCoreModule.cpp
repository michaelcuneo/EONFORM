#include "Modules/ModuleManager.h"

#include "GaeaAngleNode.h"
#include "GaeaAutoLevelNode.h"
#include "GaeaBlurNode.h"
#include "GaeaClampNode.h"
#include "GaeaCombineNode.h"
#include "GaeaCurvatureNode.h"
#include "GaeaDenoiseNode.h"
#include "GaeaFlipNode.h"
#include "GaeaHeightNode.h"
#include "GaeaInvertNode.h"
#include "GaeaMultiCombineNode.h"
#include "GaeaSharpenNode.h"
#include "GaeaSineNode.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosionNode.h"
#include "GaeaThresholdNode.h"
#include "GaeaTransformNode.h"

class FCodenameGaeaCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGaeaTerrainNodeDescriptorRegistry::RegisterBuiltIns();

		FGaeaTerrainNodeDescriptor HydraulicDescriptor;
		if (FGaeaTerrainNodeDescriptorRegistry::Get(GaeaTerrainNodeTypes::HydraulicErosion, HydraulicDescriptor))
		{
			HydraulicDescriptor.DisplayName = TEXT("Hydraulic Erosion");
			FGaeaTerrainNodeDescriptorRegistry::Register(HydraulicDescriptor);
		}

		RegisterGaeaThermalErosionNode();
		RegisterGaeaCurvatureNode();
		RegisterGaeaHeightNode();
		RegisterGaeaAngleNode();
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
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
