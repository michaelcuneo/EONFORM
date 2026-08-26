#include "SEonformTerrainGraphPanel.h"

bool SEonformTerrainGraphPanel::EvaluateSelectedNodePreview()
{
	UEonformEditorGraphNode* PreviewNode = SelectedNode.Get();
	if (!PreviewNode || PreviewNode->RecipeNodeType == EonformEditorNodeTypes::TerrainOutput)
	{
		ClearInspectionPreview();
		return true;
	}

	RequestInspectionEvaluationAsync(PreviewNode->RecipeNodeId);
	return true;
}
