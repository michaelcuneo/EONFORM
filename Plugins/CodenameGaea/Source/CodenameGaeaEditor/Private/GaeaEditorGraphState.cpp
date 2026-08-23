#include "GaeaEditorGraph.h"

UGaeaEditorGraph::UGaeaEditorGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditable = true;
}

void UGaeaEditorGraph::SetActivity(EGaeaEditorGraphActivity InActivity)
{
	Activity = InActivity;
}
