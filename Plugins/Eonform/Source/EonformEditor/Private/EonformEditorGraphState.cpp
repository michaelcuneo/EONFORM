#include "EonformEditorGraph.h"

UEonformEditorGraph::UEonformEditorGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditable = true;
}

void UEonformEditorGraph::SetActivity(EEonformEditorGraphActivity InActivity)
{
	Activity = InActivity;
}
