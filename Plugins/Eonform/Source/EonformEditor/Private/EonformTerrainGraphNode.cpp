#include "EonformTerrainGraphNode.h"

#include "DesktopPlatformModule.h"
#include "EonformEditorGraph.h"
#include "EonformTerrainRecipe.h"
#include "HAL/PlatformTime.h"
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
	TSharedRef<SWidget> MakeFilePickerContent(UEonformEditorGraphNode* Node)
	{
		TWeakObjectPtr<UEonformEditorGraphNode> WeakNode(Node);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(6.0f, 4.0f, 2.0f, 4.0f)
			[
				SNew(SEditableTextBox)
				.IsReadOnly(true)
				.ToolTipText(FText::FromString(TEXT("Selected raster file. GeoTIFF/TIFF, PNG, JPEG, BMP, and OpenEXR are supported.")))
				.Text_Lambda([WeakNode]()
				{
					if (const UEonformEditorGraphNode* Current = WeakNode.Get())
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
				.ToolTipText(FText::FromString(TEXT("Choose a terrain/image file, including GeoTIFF (.tif/.tiff)")))
				.OnClicked_Lambda([WeakNode]()
				{
					UEonformEditorGraphNode* Current = WeakNode.Get();
					if (!Current) return FReply::Handled();

					IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
					if (!DesktopPlatform) return FReply::Handled();

					TArray<FString> SelectedFiles;
					const FString Existing = Current->NameParameters.FindRef(TEXT("File")).ToString();
					const FString StartDirectory = Existing.IsEmpty() ? FString() : FPaths::GetPath(Existing);
					const FString Filter = TEXT("Terrain and Image Files (*.tif;*.tiff;*.png;*.jpg;*.jpeg;*.bmp;*.exr)|*.tif;*.tiff;*.png;*.jpg;*.jpeg;*.bmp;*.exr|GeoTIFF / TIFF (*.tif;*.tiff)|*.tif;*.tiff|PNG (*.png)|*.png|JPEG (*.jpg;*.jpeg)|*.jpg;*.jpeg|Bitmap (*.bmp)|*.bmp|OpenEXR (*.exr)|*.exr|All Files (*.*)|*.*");
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

void SEonformTerrainGraphNode::Construct(const FArguments& InArgs, UEdGraphNode* InNode)
{
	GraphNode = InNode;
	LastObservedActivity = EEonformEditorGraphActivity::Idle;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SEonformTerrainGraphNode::UpdateGraphNode()
{
	InputPins.Reset();
	OutputPins.Reset();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	UEonformEditorGraphNode* EonformNode = Cast<UEonformEditorGraphNode>(GraphNode);
	const bool bIsFileNode = EonformNode && EonformNode->RecipeNodeType == EonformTerrainNodeTypes::File;

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
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.Body"))
			.BorderBackgroundColor(this, &SEonformTerrainGraphNode::GetSolveStatusColor)
			.Padding(FMargin(8.0f, 2.0f))
			.Visibility_Lambda([this]()
			{
				const UEonformEditorGraph* Graph = GraphNode ? Cast<UEonformEditorGraph>(GraphNode->GetGraph()) : nullptr;
				return Graph && Graph->IsBusy() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Text(this, &SEonformTerrainGraphNode::GetSolveStatusText)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FLinearColor::White)
			]
		];

	if (bIsFileNode)
	{
		Body->AddSlot()
		.AutoHeight()
		[
			MakeFilePickerContent(EonformNode)
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
		.BorderBackgroundColor(this, &SEonformTerrainGraphNode::GetSolveGlowColor)
		.Padding(FMargin(4.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.Body"))
			.Padding(0.0f)
			[
				Body
			]
		]
	];

	CreatePinWidgets();
}

TSharedPtr<SGraphPin> SEonformTerrainGraphNode::CreatePinWidget(UEdGraphPin* Pin) const
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

void SEonformTerrainGraphNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UEonformEditorGraph* Graph = GraphNode ? Cast<UEonformEditorGraph>(GraphNode->GetGraph()) : nullptr;
	const EEonformEditorGraphActivity CurrentActivity = Graph ? Graph->GetActivity() : EEonformEditorGraphActivity::Idle;

	if (CurrentActivity != LastObservedActivity)
	{
		LastObservedActivity = CurrentActivity;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	if (CurrentActivity != EEonformEditorGraphActivity::Idle)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FSlateColor SEonformTerrainGraphNode::GetSolveGlowColor() const
{
	const UEonformEditorGraph* Graph = GraphNode ? Cast<UEonformEditorGraph>(GraphNode->GetGraph()) : nullptr;
	if (!Graph || !Graph->IsBusy())
	{
		return FLinearColor(0.035f, 0.035f, 0.04f, 1.0f);
	}

	const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds() * 7.0));
	if (Graph->GetActivity() == EEonformEditorGraphActivity::Analyzing)
	{
		return FLinearColor(0.08f, 0.20f + 0.28f * Pulse, 0.42f + 0.38f * Pulse, 1.0f);
	}
	return FLinearColor(0.05f, 0.42f + 0.42f * Pulse, 0.78f + 0.22f * Pulse, 1.0f);
}

FSlateColor SEonformTerrainGraphNode::GetSolveStatusColor() const
{
	const UEonformEditorGraph* Graph = GraphNode ? Cast<UEonformEditorGraph>(GraphNode->GetGraph()) : nullptr;
	if (!Graph || !Graph->IsBusy()) return FLinearColor::Transparent;

	const float Pulse = 0.65f + 0.35f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds() * 7.0));
	return Graph->GetActivity() == EEonformEditorGraphActivity::Analyzing
		? FLinearColor(0.08f, 0.32f, 0.68f, Pulse)
		: FLinearColor(0.02f, 0.52f, 1.0f, Pulse);
}

FText SEonformTerrainGraphNode::GetSolveStatusText() const
{
	const UEonformEditorGraph* Graph = GraphNode ? Cast<UEonformEditorGraph>(GraphNode->GetGraph()) : nullptr;
	if (!Graph) return FText::GetEmpty();

	switch (Graph->GetActivity())
	{
	case EEonformEditorGraphActivity::Solving:
		return FText::FromString(TEXT("SOLVING TERRAIN..."));
	case EEonformEditorGraphActivity::Analyzing:
		return FText::FromString(TEXT("ANALYZING TERRAIN..."));
	default:
		return FText::GetEmpty();
	}
}

TSharedPtr<SGraphNode> FEonformTerrainGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (!Cast<UEonformEditorGraphNode>(InNode))
	{
		return nullptr;
	}

	return SNew(SEonformTerrainGraphNode, InNode);
}
