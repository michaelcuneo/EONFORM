#include "SGaeaTerrainGraphPanel.h"

bool SGaeaTerrainGraphPanel::EvaluateSelectedNodePreview()
{
	UGaeaEditorGraphNode* PreviewNode = SelectedNode.Get();
	if (!PreviewNode || PreviewNode->RecipeNodeType == GaeaEditorNodeTypes::TerrainOutput)
	{
		ClearInspectionPreview();
		return true;
	}

	RequestInspectionEvaluationAsync(PreviewNode->RecipeNodeId);
	return true;
}
