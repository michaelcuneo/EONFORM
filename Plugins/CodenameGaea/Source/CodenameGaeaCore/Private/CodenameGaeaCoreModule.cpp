#include "Modules/ModuleManager.h"

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
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
