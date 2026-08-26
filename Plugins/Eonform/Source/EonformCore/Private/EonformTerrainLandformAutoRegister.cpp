#include "EonformTerrainLandformNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FEonformTerrainLandformRegistrationHook
	{
		FEonformTerrainLandformRegistrationHook()
		{
			FCoreDelegates::GetOnPostEngineInit().AddStatic(&FEonformTerrainLandformRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterEonformTerrainLandformNodes();
		}
	};

	FEonformTerrainLandformRegistrationHook GTerrainLandformRegistrationHook;
}
