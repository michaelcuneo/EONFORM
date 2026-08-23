#include "GaeaTextureDeriveNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FGaeaTextureDeriveRegistrationHook
	{
		FGaeaTextureDeriveRegistrationHook()
		{
			FCoreDelegates::OnPostEngineInit.AddStatic(&FGaeaTextureDeriveRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterGaeaTextureDeriveNodes();
		}
	};

	FGaeaTextureDeriveRegistrationHook GTextureDeriveRegistrationHook;
}
