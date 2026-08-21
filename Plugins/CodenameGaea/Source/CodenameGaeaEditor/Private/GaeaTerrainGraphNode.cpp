#include "GaeaTerrainGraphNode.h"

#include "DesktopPlatformModule.h"
#include "GaeaEditorGraph.h"
#include "GaeaTerrainRecipe.h"
#include "IDesktopPlatform.h"
#include "SGraphPin.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	TSharedRef<SWidget> MakeFilePickerContent(UGaeaEditorGraphNode* Node)
	{
		TWeakObjectPtr<UGaeaEditorGraphNode> WeakNode(Node);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(6.0f, 4.0f, 2.0f, 4.0f)
			[
				SNew(SEditableTextBox)
				.IsReadOnly(true)
				.ToolTipText(FText::FromString(TEXT("Selected raster file.")))
				.Text_Lambda([WeakNode]()
				{
					if (const UGaeaEditorGraphNode* Current = WeakNode.Get())
					{
						const FName Path = Current->NameParameters.FindRef(TEXT("File"));
						return Path.IsNone() ? FText::FromString(TEXT("No file selected")) : FText::FromString(Path.ToString());
					}
					return FText::GetEmpty();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 4.0f, 6.0f, 4.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("...")))
				.ToolTipText(FText::FromString(TEXT("Choose a terrain/image file")))
				.OnClicked_Lambda([WeakNode]()
				{
					UGaeaEditorGraphNode* Current = WeakNode.Get();
					if (!Current) return FReply::Handled();

					IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
					if (!DesktopPlatform) return FReply::Handled();

					TArray<FString> SelectedFiles;
					const FString Existing = Current->NameParameters.FindRef(TEXT("File")).ToString();
					const FString StartDirectory = Existing.IsEmpty() ? FString() : FPaths::GetPath(Existing);
					const FString Filter = TEXT("Terrain and Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.exr)|*.png;*.jpg;*.jpeg;*.bmp;*.exr|PNG (*.png)|*.png|JPEG (*.jpg;*.jpeg)|*.jpg;*.jpeg|Bitmap (*.bmp)|*.bmp|OpenEXR (*.exr)|*.exr|All Files (*.*)|*.*");
					if (DesktopPlatform->OpenFileDialog(
						nullptr,
						TEXT("Choose Gaea File Input"),
						StartDirectory,
						TEXT(""),
						Filter,
						EFileDialogFlags::None,
						SelectedFiles)
						&& SelectedFiles.Num() > 0)
					{
						Current->Modify();
						Current->NameParameters.Add(TEXT("File"), FName(*FPaths::ConvertRelativePathToFull(SelectedFiles[0])));
					}
					return FReply::Handled();
				})
			];
	}
}

void SGaeaTerrainGraphNode::Construct(const FArguments& InArgs, UEdGraphNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SGaeaTerrainGraphNode::UpdateGraphNode()
{
	InputPins.Reset();
	OutputPins.Reset();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	UGaeaEditorGraphNode* GaeaNode = Cast<UGaeaEditorGraphNode>(GraphNode);
	const bool bIsFileNode = GaeaNode && GaeaNode->RecipeNodeType == GaeaTerrainNodeTypes::File;

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.TitleBackground"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f, 1.0f))
			.Padding(FMargin(8.0f, 4.0f))
			[
				SNew(STextBlock)
				.Text(GraphNode ? GraphNode->GetNodeTitle(ENodeTitleType::FullTitle) : FText::GetEmpty())
				.ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f))
			]
		];

	if (bIsFileNode)
	{
		Body->AddSlot()
		.AutoHeight()
		[
			MakeFilePickerContent(GaeaNode)
		];
	}

	Body->AddSlot()
	.AutoHeight()
	[
		CreateNodeContentArea()
	];

	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Graph.Node.Body"))
		.Padding(0.0f)
		[
			Body
		]
	];

	CreatePinWidgets();
}

TSharedPtr<SGraphPin> SGaeaTerrainGraphNode::CreatePinWidget(UEdGraphPin* Pin) const
{
	if (!Pin)
	{
		return nullptr;
	}

	TSharedPtr<SGraphPin> PinWidget = SGraphNode::CreatePinWidget(Pin);
	if (!PinWidget.IsValid())
	{
		PinWidget = SNew(SGraphPin, Pin);
	}

	PinWidget->EnableDragAndDrop(true);
	return PinWidget;
}

TSharedPtr<SGraphNode> FGaeaTerrainGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (!Cast<UGaeaEditorGraphNode>(InNode))
	{
		return nullptr;
	}

	return SNew(SGaeaTerrainGraphNode, InNode);
}
