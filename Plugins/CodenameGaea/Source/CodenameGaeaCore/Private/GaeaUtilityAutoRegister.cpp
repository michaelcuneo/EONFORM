#include "GaeaUtilityNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FGaeaUtilityRegistrationHook
	{
		FGaeaUtilityRegistrationHook()
		{
			FCoreDelegates::OnPostEngineInit.AddStatic(&FGaeaUtilityRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterGaeaUtilityNodes();
		}
	};

	FGaeaUtilityRegistrationHook GUtilityRegistrationHook;
}
