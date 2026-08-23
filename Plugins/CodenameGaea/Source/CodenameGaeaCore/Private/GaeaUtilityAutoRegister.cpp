#include "GaeaUtilityNodes.h"

#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"

namespace
{
	struct FGaeaUtilityRegistrationHook
	{
		FGaeaUtilityRegistrationHook()
		{
			if (GEngine)
			{
				RegisterNodes();
			}
			else
			{
				FCoreDelegates::OnPostEngineInit.AddStatic(&FGaeaUtilityRegistrationHook::RegisterNodes);
			}
		}

		static void RegisterNodes()
		{
			RegisterGaeaUtilityNodes();
		}
	};

	FGaeaUtilityRegistrationHook GUtilityRegistrationHook;
}
