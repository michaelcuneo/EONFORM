#include "EonformReferenceFidelityExtendedNodes.h"

#include "EonformGroundTextureNode.h"
#include "EonformRockNoiseNode.h"
#include "EonformStratifyNode.h"

void RegisterEonformReferenceFidelityExtendedNodes()
{
	RegisterEonformRockNoiseNode();
	RegisterEonformGroundTextureNode();
	RegisterEonformStratifyNode();
}
