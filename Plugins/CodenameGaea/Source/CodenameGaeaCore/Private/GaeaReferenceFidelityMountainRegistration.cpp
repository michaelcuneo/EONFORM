#include "GaeaReferenceFidelityMountainNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	void RegisterAuditedMountainDependencies()
	{
		RegisterGaeaReferenceFidelityMountainNodes();
	}

	struct FGaeaMountainFidelityRegistration
	{
		FGaeaMountainFidelityRegistration()
		{
			// CodenameGaeaCore loads in the plugin's Default phase. Registering on
			// PostEngineInit therefore happens after every normal Core startup
			// registration, so audited implementations are authoritative.
			FCoreDelegates::GetOnPostEngineInit().AddStatic(&RegisterAuditedMountainDependencies);
		}
	};

	FGaeaMountainFidelityRegistration GMountainFidelityRegistration;
}
