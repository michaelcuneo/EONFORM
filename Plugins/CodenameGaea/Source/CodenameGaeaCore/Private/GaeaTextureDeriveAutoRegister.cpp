#include "GaeaTextureDeriveNodes.h"

namespace
{
	struct FGaeaTextureDeriveAutoRegister
	{
		FGaeaTextureDeriveAutoRegister()
		{
			RegisterGaeaTextureDeriveNodes();
		}
	};

	FGaeaTextureDeriveAutoRegister GTextureDeriveAutoRegister;
}
