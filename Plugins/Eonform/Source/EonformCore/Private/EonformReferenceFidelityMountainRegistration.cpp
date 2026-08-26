#include "EonformReferenceFidelityMountainNodes.h"

#include "Misc/CoreDelegates.h"

namespace
{
	void RegisterAuditedMountainDependencies()
	{
		RegisterEonformReferenceFidelityMountainNodes();
	}

	struct FEonformMountainFidelityRegistration
	{
		FEonformMountainFidelityRegistration()
		{
			// EonformCore loads in the plugin's Default phase. Registering on
			// PostEngineInit therefore happens after every normal Core startup
			// registration, so audited implementations are authoritative.
			FCoreDelegates::GetOnPostEngineInit().AddStatic(&RegisterAuditedMountainDependencies);
		}
	};

	FEonformMountainFidelityRegistration GMountainFidelityRegistration;
}
