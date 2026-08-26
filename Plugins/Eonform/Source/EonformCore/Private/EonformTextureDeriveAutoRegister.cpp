#include "EonformTextureDeriveNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	struct FEonformTextureDeriveRegistrationHook
	{
		FEonformTextureDeriveRegistrationHook()
		{
			FCoreDelegates::OnPostEngineInit.AddStatic(&FEonformTextureDeriveRegistrationHook::RegisterNodes);
		}

		static void RegisterNodes()
		{
			RegisterEonformTextureDeriveNodes();
		}
	};

	FEonformTextureDeriveRegistrationHook GTextureDeriveRegistrationHook;
}
