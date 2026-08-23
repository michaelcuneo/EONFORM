#include "GaeaTerrainLandformNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FGaeaTerrainLandformRegistrationHook
	{
		FGaeaTerrainLandformRegistrationHook()
		{
			FCoreDelegates::GetOnPostEngineInit().AddStatic(&FGaeaTerrainLandformRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterGaeaTerrainLandformNodes();
		}
	};

	FGaeaTerrainLandformRegistrationHook GTerrainLandformRegistrationHook;
}
