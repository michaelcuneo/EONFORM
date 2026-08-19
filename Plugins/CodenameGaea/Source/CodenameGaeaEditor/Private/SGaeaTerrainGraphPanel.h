#pragma once

#include "CoreMinimal.h"
#include "GaeaEditorGraph.h"
#include "GaeaTerrainRecipe.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SGraphEditor;

class SGaeaTerrainGraphPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGaeaTerrainGraphPanel) {}
		SLATE_EVENT(FSimpleDelegate, OnEvaluated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void BuildDefaultRecipeAndGraph();
	FReply EvaluateGraph();
	FText GetStatusText() const;

	FGaeaTerrainRecipe Recipe;
	TStrongObjectPtr<UGaeaEditorGraph> EditorGraph;
	TSharedPtr<SGraphEditor> GraphEditor;
	FSimpleDelegate OnEvaluated;
	FText StatusText;
};
