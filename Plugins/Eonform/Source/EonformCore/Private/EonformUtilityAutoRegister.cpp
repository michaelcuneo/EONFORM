#include "EonformUtilityNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FEonformUtilityRegistrationHook
	{
		FEonformUtilityRegistrationHook()
		{
			FCoreDelegates::GetOnPostEngineInit().AddStatic(&FEonformUtilityRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterEonformUtilityNodes();
		}
	};

	FEonformUtilityRegistrationHook GUtilityRegistrationHook;
}
