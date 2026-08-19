#include "Modules/ModuleManager.h"

#include "GaeaThermalErosionNode.h"

class FCodenameGaeaCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterGaeaThermalErosionNode();
	}
};

IMPLEMENT_MODULE(FCodenameGaeaCoreModule, CodenameGaeaCore)
