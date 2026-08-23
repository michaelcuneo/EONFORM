#include "GaeaTerrainLandformNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FGaeaTerrainLandformRegistrationHook
	{
		FGaeaTerrainLandformRegistrationHook()
		{
			// Descriptor/node registries are additive and duplicate registration replaces
			// the same key, so register immediately for hot reload and again after engine
			// init for normal startup ordering.
			RegisterNodes();
			FCoreDelegates::GetOnPostEngineInit().AddStatic(&FGaeaTerrainLandformRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterGaeaTerrainLandformNodes();
		}
	};

	FGaeaTerrainLandformRegistrationHook GTerrainLandformRegistrationHook;
}
