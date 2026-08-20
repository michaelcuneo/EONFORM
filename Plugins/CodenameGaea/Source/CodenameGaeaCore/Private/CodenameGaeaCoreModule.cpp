#include "Modules/ModuleManager.h"

#include "GaeaCurvatureNode.h"
#include "GaeaElevationNode.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "GaeaTerrainRegionsNode.h"
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
		RegisterGaeaElevationNode();
		RegisterGaeaTerrainRegionsNode();
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
