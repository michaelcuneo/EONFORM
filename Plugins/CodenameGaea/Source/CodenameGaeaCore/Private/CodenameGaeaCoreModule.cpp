#include "Modules/ModuleManager.h"

#include "GaeaAngleNode.h"
#include "GaeaAutoLevelNode.h"
#include "GaeaBlurNode.h"
#include "GaeaClampNode.h"
#include "GaeaCombineNode.h"
#include "GaeaCurvatureNode.h"
#include "GaeaHeightNode.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaThermalErosionNode.h"

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
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
