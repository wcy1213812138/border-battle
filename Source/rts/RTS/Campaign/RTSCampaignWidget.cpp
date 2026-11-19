#include "Campaign/RTSCampaignWidget.h"
#include "EngineUtils.h"
#include "Campaign/RTSCampaignEntry.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignAction.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Campaign/RTSCampaignResourceNames.h"
#include "Campaign/RTSCampaignMinimap.h"
#include "Campaign/RTSCampaignSettingsWidget.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Campaign/RTSCampaignInputShield.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Core/RTSDisplayNames.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "UI/RTSUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Kismet/GameplayStatics.h"
#include "Data/RTSBalanceSubsystem.h"
#include "UI/RTSHUD.h"
#include "Economy/RTSResourceComponent.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"

namespace
{
 FLinearColor Accent(int32 Page)
 {
  const FLinearColor C[]={FLinearColor(.68f,.40f,1.f),FLinearColor(.22f,.85f,.76f),FLinearColor(1.f,.73f,.28f),FLinearColor(.55f,.7f,.88f)};
  return C[FMath::Clamp(Page,0,3)];
 }
 UTextBlock* Label(UWidgetTree* Tree,const FString& Text,int32 Size,FLinearColor Color)
 {
  auto* L=Tree->ConstructWidget<UTextBlock>(); RTSUIStyle::Text(L,Size,Color); L->SetText(FText::FromString(Text)); L->SetAutoWrapText(true); return L;
 }
 void Dark(UBorder* B,FLinearColor Color=FLinearColor(.035f,.045f,.065f,.97f)) { B->SetBrushColor(Color); B->SetPadding(FMargin(14)); }
 void ButtonStyle(UButton* B,FLinearColor Color,bool Selected=false)
 {
  FButtonStyle S=B->GetStyle();
  S.Normal.TintColor=FSlateColor(Selected?Color*.5f:FLinearColor(.085f,.105f,.14f,1));
  S.Hovered.TintColor=FSlateColor(Color*.45f); S.Pressed.TintColor=FSlateColor(Color*.65f);
  S.NormalPadding=FMargin(12,10); S.PressedPadding=FMargin(12,11,12,9); B->SetStyle(S);
 }
}
void URTSCampaignWidget::AddSection(UVerticalBox* Column,const FString& Caption)
{
 auto* Card=WidgetTree->ConstructWidget<UBorder>(); Dark(Card,FLinearColor(.075f,.09f,.12f,1));
 Card->SetContent(Label(WidgetTree,Caption,13,Accent(Pages?Pages->GetChildrenCount()-1:0)));
 Column->AddChildToVerticalBox(Card)->SetPadding(FMargin(0,16,0,6));
}
UButton* URTSCampaignWidget::AddRow(UVerticalBox* Column,const FString& Caption,FName Function,TObjectPtr<UTextBlock>* OutLabel,FName PortraitKind)
{
 auto* B=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Function); auto* L=Label(WidgetTree,Caption,13,FLinearColor(.9f,.93f,.98f));
 B->SetContent(L); ButtonStyle(B,Accent(Pages?Pages->GetChildrenCount()-1:0));
 const FString Action=Function.ToString();
 const TCHAR* UnitKind=Action==TEXT("RecruitWorker")?TEXT("Peasant"):(Action==TEXT("RecruitSword") || Action==TEXT("UpgradeSword"))?TEXT("Swordman"):(Action==TEXT("RecruitArcher") || Action==TEXT("UpgradeArcher"))?TEXT("Archer"):(Action==TEXT("RecruitSpear") || Action==TEXT("UnlockSpear"))?TEXT("Spearman"):(Action==TEXT("RecruitScout") || Action==TEXT("UnlockScout"))?TEXT("Scout"):(Action==TEXT("RecruitPriest") || Action==TEXT("UnlockPriest"))?TEXT("Priest"):nullptr;
 if(UnitKind || !PortraitKind.IsNone())
 {
  auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();B->SetContent(Row);
  if(auto* ContentSlot=Cast<UButtonSlot>(Row->Slot)){ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);}
  auto* Frame=WidgetTree->ConstructWidget<USizeBox>();Frame->SetWidthOverride(48);Frame->SetHeightOverride(48);
  auto* Image=WidgetTree->ConstructWidget<UImage>();
  const FString PortraitPath=UnitKind?FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_Unit_%s.T_Unit_%s"),UnitKind,UnitKind):FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_%s_Portrait.T_%s_Portrait"),*PortraitKind.ToString(),*PortraitKind.ToString());
  Image->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr,*PortraitPath));
  Image->SetVisibility(ESlateVisibility::HitTestInvisible);Frame->SetContent(Image);
  auto* IconSlot=Row->AddChildToHorizontalBox(Frame);IconSlot->SetPadding(FMargin(0,0,10,0));IconSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));IconSlot->SetVerticalAlignment(VAlign_Center);
  auto* TextSlot=Row->AddChildToHorizontalBox(L);TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TextSlot->SetVerticalAlignment(VAlign_Center);
 }
 FScriptDelegate D;D.BindUFunction(this,Function);B->OnClicked.Add(D);
 Column->AddChildToVerticalBox(B)->SetPadding(FMargin(0,3)); if(OutLabel)*OutLabel=L; return B;
}
UButton* URTSCampaignWidget::AddNavButton(UHorizontalBox* Bar,const FString& Caption,FName Function,int32 PageIndex)
{
 auto* B=WidgetTree->ConstructWidget<UButton>();auto* L=Label(WidgetTree,Caption,13,Accent(PageIndex));L->SetAutoWrapText(false);L->SetJustification(ETextJustify::Center);
 B->SetContent(L);ButtonStyle(B,Accent(PageIndex),PageIndex==ActivePage);
 if(auto* ContentSlot=Cast<UButtonSlot>(L->Slot))ContentSlot->SetHorizontalAlignment(HAlign_Fill);
 FScriptDelegate D;D.BindUFunction(this,Function);B->OnClicked.Add(D);
 auto* Cell=Bar->AddChildToHorizontalBox(B);Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Cell->SetPadding(FMargin(2));
 NavButtons.Add(B);NavLabels.Add(L);NavCaptions.Add(Caption);return B;
}
UVerticalBox* URTSCampaignWidget::AddPage(const FString& Name)
{
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(*Name));Pages->AddChild(Box);return Box;
}
void URTSCampaignWidget::SelectPage(int32 Index)
{
 ActivePage=FMath::Clamp(Index,0,3);
 if(ActivePage==0){if(ToolsPanel)ToolsPanel->SetVisibility(ESlateVisibility::Visible);if(ToolPages)ToolPages->SetActiveWidgetIndex(0);}
 else if(ActivePage==3)ShowSettings();
 if(auto* C=ARTSCampaign::Get(this))C->NotifyPageOpened(ActivePage);
}
void URTSCampaignWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 AlertBanner=WidgetTree->ConstructWidget<UBorder>();Dark(AlertBanner,FLinearColor(.28f,.025f,.025f,.95f));
 auto* AlertCell=Canvas->AddChildToCanvas(AlertBanner);AlertCell->SetAnchors(FAnchors(.5f,0));AlertCell->SetAlignment(FVector2D(.5f,0));AlertCell->SetPosition(FVector2D(0,132));AlertCell->SetAutoSize(true);AlertCell->SetZOrder(8);
 auto* AlertSize=WidgetTree->ConstructWidget<USizeBox>();AlertSize->SetWidthOverride(560);AlertBanner->SetContent(AlertSize);AlertLabel=Label(WidgetTree,TEXT(""),16,FLinearColor(1,.85f,.75f));AlertSize->SetContent(AlertLabel);AlertBanner->SetVisibility(ESlateVisibility::Collapsed);
 StockBoard=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CampaignStockBoard"));Dark(StockBoard,FLinearColor(.025f,.055f,.085f,.98f));StockBoard->SetPadding(FMargin(12,8));
 auto* StockCell=Canvas->AddChildToCanvas(StockBoard);StockCell->SetAnchors(FAnchors(1,0));StockCell->SetAlignment(FVector2D(1,0));StockCell->SetPosition(FVector2D(-4,12));StockCell->SetAutoSize(true);StockCell->SetZOrder(4);
 auto* StockColumn=WidgetTree->ConstructWidget<UVerticalBox>();StockBoard->SetContent(StockColumn);
 for(int32 RowIndex=0;RowIndex<2;++RowIndex)
 {
  auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();StockColumn->AddChildToVerticalBox(Row);
  for(int32 Col=0;Col<4;++Col)
  {
   const int32 I=RowIndex*4+Col;auto* Cell=WidgetTree->ConstructWidget<USizeBox>();Cell->SetWidthOverride(126);Cell->SetHeightOverride(56);Row->AddChildToHorizontalBox(Cell)->SetPadding(FMargin(2));
   auto* Tile=WidgetTree->ConstructWidget<UBorder>();Tile->SetBrushColor(FLinearColor(.06f,.095f,.13f,1));Tile->SetPadding(FMargin(6,4));Cell->SetContent(Tile);
   auto* Content=WidgetTree->ConstructWidget<UHorizontalBox>();Tile->SetContent(Content);
   const TCHAR* Icons[]={TEXT("Wood"),TEXT("Food"),TEXT("IronOre"),TEXT("RefinedIron"),TEXT("Amber"),TEXT("Crystal"),TEXT("Gold"),TEXT("Mana")};
   auto* IconFrame=WidgetTree->ConstructWidget<USizeBox>();IconFrame->SetWidthOverride(36);IconFrame->SetHeightOverride(36);
   auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("CampaignStockIcon%d"),I)));Icon->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_Resource%s_Portrait"),Icons[I])));IconFrame->SetContent(Icon);Content->AddChildToHorizontalBox(IconFrame)->SetVerticalAlignment(VAlign_Center);
   auto* Values=WidgetTree->ConstructWidget<UVerticalBox>();auto* ValuesSlot=Content->AddChildToHorizontalBox(Values);ValuesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ValuesSlot->SetPadding(FMargin(6,0,0,0));ValuesSlot->SetVerticalAlignment(VAlign_Center);
   auto* Name=Label(WidgetTree,RTSCampaignResourceNames::Name(ERTSResource(I)),11,FLinearColor(.65f,.75f,.82f));Name->SetAutoWrapText(false);Values->AddChildToVerticalBox(Name);
   auto* NumberFrame=WidgetTree->ConstructWidget<USizeBox>();NumberFrame->SetHeightOverride(25);Values->AddChildToVerticalBox(NumberFrame);
   auto* NumberScale=WidgetTree->ConstructWidget<UScaleBox>();NumberScale->SetStretch(EStretch::ScaleToFit);NumberScale->SetStretchDirection(EStretchDirection::DownOnly);NumberFrame->SetContent(NumberScale);
   auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("CampaignStock%d"),I)));RTSUIStyle::Text(Text,16,FLinearColor(.98f,.94f,.82f));Text->SetAutoWrapText(false);NumberScale->SetContent(Text);if(auto* NumberSlot=Cast<UScaleBoxSlot>(Text->Slot))NumberSlot->SetHorizontalAlignment(HAlign_Left);StockLabels.Add(Text);
   Tile->SetToolTipText(FText::FromString(FString(RTSCampaignResourceNames::Name(ERTSResource(I)))+TEXT(" · 当前库存")));
  }
 }
 PopulationLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CampaignPopulation"));
 RTSUIStyle::Text(PopulationLabel,15,FLinearColor(.98f,.94f,.82f));PopulationLabel->SetAutoWrapText(false);
 StockColumn->AddChildToVerticalBox(PopulationLabel)->SetPadding(FMargin(4,6,4,0));
 PopulationLabel->SetToolTipText(FText::FromString(TEXT("当前人口 / 最大人口；排队训练也占用人口名额。住宅扩建提升最大人口。")));
 ResourceDetails=WidgetTree->ConstructWidget<UBorder>();Dark(ResourceDetails);
 auto* ResourceCell=Canvas->AddChildToCanvas(ResourceDetails);ResourceCell->SetAnchors(FAnchors(1,1));ResourceCell->SetAlignment(FVector2D(1,1));ResourceCell->SetPosition(FVector2D(-372,-16));ResourceCell->SetAutoSize(true);
 auto* ResourceSize=WidgetTree->ConstructWidget<USizeBox>();ResourceSize->SetWidthOverride(370);ResourceDetails->SetContent(ResourceSize);
 auto* ResourceRow=WidgetTree->ConstructWidget<UHorizontalBox>();ResourceSize->SetContent(ResourceRow);
 auto* ResourceImageSize=WidgetTree->ConstructWidget<USizeBox>();ResourceImageSize->SetWidthOverride(96);ResourceImageSize->SetHeightOverride(96);
 ResourcePortrait=WidgetTree->ConstructWidget<UImage>();ResourceImageSize->SetContent(ResourcePortrait);ResourceRow->AddChildToHorizontalBox(ResourceImageSize)->SetPadding(FMargin(0,0,12,0));
 ResourceDetailsText=Label(WidgetTree,TEXT(""),13,FLinearColor(.93f,.96f,1));auto* ResourceTextSlot=ResourceRow->AddChildToHorizontalBox(ResourceDetailsText);ResourceTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 ResourceDetails->SetVisibility(ESlateVisibility::Collapsed);
 Board=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CampaignBoard"));Dark(Board);
 auto* BoardCell=Canvas->AddChildToCanvas(Board);BoardCell->SetAnchors(FAnchors(1,0,1,1));BoardCell->SetAlignment(FVector2D(1,0));BoardCell->SetOffsets(FMargin(-4,164,0,4));BoardCell->SetSize(FVector2D(360,0));
 PanelSize=WidgetTree->ConstructWidget<USizeBox>();PanelSize->SetWidthOverride(360);PanelSize->SetHeightOverride(556);Board->SetContent(PanelSize);Board->SetClipping(EWidgetClipping::ClipToBounds);
 auto* Outer=WidgetTree->ConstructWidget<UVerticalBox>();PanelSize->SetContent(Outer);
 auto* Brand=Label(WidgetTree,TEXT("BORDERLANDS  /  当前对象"),11,FLinearColor(.65f,.74f,.8f));Outer->AddChildToVerticalBox(Brand);
 auto* MapBoard=WidgetTree->ConstructWidget<UBorder>();Dark(MapBoard);auto* MapCell=Canvas->AddChildToCanvas(MapBoard);MapCell->SetPosition(FVector2D(16,16));MapCell->SetAutoSize(true);
 auto* MapColumn=WidgetTree->ConstructWidget<UVerticalBox>();MapBoard->SetContent(MapColumn);
 auto* MapHeader=WidgetTree->ConstructWidget<UHorizontalBox>();MapColumn->AddChildToVerticalBox(MapHeader);
 auto* MapCaption=Label(WidgetTree,TEXT("小地图 · 左键拖动镜头"),11,FLinearColor(.8f,.85f,.75f));MapHeader->AddChildToHorizontalBox(MapCaption)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 auto* FullMapButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("ResetCampaignMinimap"));ButtonStyle(FullMapButton,Accent(1));FullMapButton->SetContent(Label(WidgetTree,TEXT("全图"),11,FLinearColor::White));FullMapButton->OnClicked.AddDynamic(this,&URTSCampaignWidget::ResetMinimap);MapHeader->AddChildToHorizontalBox(FullMapButton);
 auto* MapSize=WidgetTree->ConstructWidget<USizeBox>();MapSize->SetWidthOverride(280);MapSize->SetHeightOverride(200);MapColumn->AddChildToVerticalBox(MapSize);
 auto* Minimap=CreateWidget<URTSCampaignMinimap>(GetOwningPlayer(),URTSCampaignMinimap::StaticClass(),TEXT("CampaignMinimap"));
 CampaignMinimap=Minimap;
 Minimap->SetVisibility(ESlateVisibility::Visible);
 Minimap->SetToolTipText(FText::FromString(TEXT("方块：大本营 · 菱形：资源 · 边缘方块：视野外大本营方向\n左键拖动镜头 · 右键移动选中单位（Shift追加） · 滚轮缩放 · 全图按钮恢复全部标记")));
 MapSize->SetContent(Minimap);
 OverviewText=Label(WidgetTree,TEXT("准备营地"),11,FLinearColor(.92f,.95f,1));MapColumn->AddChildToVerticalBox(OverviewText)->SetPadding(FMargin(0,8));
 auto* ToolBar=WidgetTree->ConstructWidget<UHorizontalBox>();MapColumn->AddChildToVerticalBox(ToolBar);
 AddNavButton(ToolBar,TEXT("战报"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,ShowCampaignPage),0);
 auto* Gear=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("OpenCampaignSettings"));ButtonStyle(Gear,Accent(3));Gear->OnClicked.AddDynamic(this,&URTSCampaignWidget::ShowSettings);Gear->SetToolTipText(FText::FromString(TEXT("设置 · 保存/读取 · 音量")));
 auto* GearRow=WidgetTree->ConstructWidget<UHorizontalBox>();Gear->SetContent(GearRow);if(auto* ContentSlot=Cast<UButtonSlot>(GearRow->Slot))ContentSlot->SetHorizontalAlignment(HAlign_Fill);
 auto* GearSize=WidgetTree->ConstructWidget<USizeBox>();GearSize->SetWidthOverride(22);GearSize->SetHeightOverride(22);auto* GearIcon=CreateWidget<URTSCampaignGearIcon>(GetOwningPlayer());GearIcon->SetVisibility(ESlateVisibility::HitTestInvisible);GearSize->SetContent(GearIcon);auto* GearIconSlot=GearRow->AddChildToHorizontalBox(GearSize);GearIconSlot->SetVerticalAlignment(VAlign_Center);GearIconSlot->SetPadding(FMargin(0,0,10,0));
 auto* GearText=Label(WidgetTree,TEXT("设置"),13,Accent(3));GearText->SetAutoWrapText(false);auto* GearTextSlot=GearRow->AddChildToHorizontalBox(GearText);GearTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));GearTextSlot->SetVerticalAlignment(VAlign_Center);
 auto* GearCell=ToolBar->AddChildToHorizontalBox(Gear);GearCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));GearCell->SetPadding(FMargin(2));
 auto* PortraitFrame=WidgetTree->ConstructWidget<UBorder>();Dark(PortraitFrame,FLinearColor(.07f,.12f,.15f));Outer->AddChildToVerticalBox(PortraitFrame)->SetPadding(FMargin(0,10));
 auto* PortraitSizeBox=WidgetTree->ConstructWidget<USizeBox>();PortraitSizeBox->SetHeightOverride(200);PortraitFrame->SetContent(PortraitSizeBox);
 BuildingPortrait=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("SelectedBuildingImage"));BuildingPortrait->SetVisibility(ESlateVisibility::Hidden);PortraitSizeBox->SetContent(BuildingPortrait);
 PageTitle=Label(WidgetTree,TEXT("未选择建筑"),25,Accent(1));Outer->AddChildToVerticalBox(PageTitle)->SetPadding(FMargin(0,8,0,4));
 PageSubtitle=Label(WidgetTree,TEXT("点击场景中的己方建筑，查看它的专属指令。"),12,FLinearColor(.7f,.78f,.82f));Outer->AddChildToVerticalBox(PageSubtitle)->SetPadding(FMargin(0,0,0,8));
 ContextText=Label(WidgetTree,TEXT(""),12,FLinearColor(.8f,.86f,.9f));Outer->AddChildToVerticalBox(ContextText)->SetPadding(FMargin(0,0,0,8));
 PageScroll=WidgetTree->ConstructWidget<UScrollBox>();Outer->AddChildToVerticalBox(PageScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 QueuePanel=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ProductionQueuePanel"));PageScroll->AddChild(QueuePanel);
 QueuePanel->AddChildToVerticalBox(Label(WidgetTree,TEXT("建造 / 训练队列"),13,FLinearColor(.75f,.88f,1.f)));
 for(int32 I=0;I<6;++I)
 {
  auto* Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("ProductionQueueRow%d"),I)));Dark(Card,FLinearColor(.055f,.085f,.12f,1));Card->SetPadding(FMargin(6));QueuePanel->AddChildToVerticalBox(Card)->SetPadding(FMargin(0,3));
  auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Card->SetContent(Row);
  auto* Frame=WidgetTree->ConstructWidget<USizeBox>();Frame->SetWidthOverride(40);Frame->SetHeightOverride(40);
  auto* Image=WidgetTree->ConstructWidget<UImage>();Frame->SetContent(Image);auto* ImageCell=Row->AddChildToHorizontalBox(Frame);ImageCell->SetPadding(FMargin(0,0,10,0));ImageCell->SetVerticalAlignment(VAlign_Center);
  auto* Text=Label(WidgetTree,TEXT(""),12,FLinearColor(.9f,.95f,1));auto* TextCell=Row->AddChildToHorizontalBox(Text);TextCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TextCell->SetVerticalAlignment(VAlign_Center);
  QueueRows.Add(Card);QueueImages.Add(Image);QueueLabels.Add(Text);QueuePortraitPaths.Add(TEXT(""));Card->SetVisibility(ESlateVisibility::Collapsed);
 }
 QueuePanel->SetVisibility(ESlateVisibility::Collapsed);
 Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>();PageScroll->AddChild(Pages);
 auto* ObjectActions=WidgetTree->ConstructWidget<UVerticalBox>();PageScroll->AddChild(ObjectActions);Pages->SetVisibility(ESlateVisibility::Collapsed);
 ToolsPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CampaignWarPanel"));Dark(ToolsPanel);auto* ToolsCell=Canvas->AddChildToCanvas(ToolsPanel);ToolsCell->SetPosition(FVector2D(336,164));ToolsCell->SetAutoSize(true);ToolsCell->SetZOrder(5);ToolsPanel->SetClipping(EWidgetClipping::ClipToBounds);
 ToolsSize=WidgetTree->ConstructWidget<USizeBox>();ToolsSize->SetWidthOverride(360);ToolsSize->SetHeightOverride(540);ToolsPanel->SetContent(ToolsSize);
 auto* ToolsColumn=WidgetTree->ConstructWidget<UVerticalBox>();ToolsSize->SetContent(ToolsColumn);AddRow(ToolsColumn,TEXT("关闭战报"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,CloseTools));
 auto* ToolsScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("CampaignWarScroll"));ToolsScroll->SetClipping(EWidgetClipping::ClipToBounds);ToolsScroll->SetScrollBarVisibility(ESlateVisibility::Visible);ToolsColumn->AddChildToVerticalBox(ToolsScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 ToolPages=WidgetTree->ConstructWidget<UWidgetSwitcher>();ToolsScroll->AddChild(ToolPages);
 auto* War=AddPage(TEXT("Page_Campaign"));AddSection(War,TEXT("01  敌方动向与出兵预告"));
 WarText=Label(WidgetTree,TEXT("等待情报"),13,FLinearColor(.85f,.81f,.96f));WarText->SetWrapTextAt(316);War->AddChildToVerticalBox(WarText)->SetPadding(FMargin(0,4,0,8));
 AddSection(War,TEXT("02  领土目标 · 点击仅定位"));
 ObjectivesText=Label(WidgetTree,TEXT(""),12,FLinearColor(.65f,.7f,.8f));ObjectivesText->SetWrapTextAt(316);War->AddChildToVerticalBox(ObjectivesText);
 AddRow(War,TEXT("黄色｜谷地铁矿前哨"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackIron));
 AddRow(War,TEXT("黄色｜南部富铁商道"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackRichIron));
 AddRow(War,TEXT("紫色｜西坡琥珀哨站"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackRefined));
 AddRow(War,TEXT("紫色｜暮棘盟要塞"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackRichRefined));
 AddRow(War,TEXT("黄色｜赤金沙团大营"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackLord));
 AddRow(War,TEXT("红色｜赤焰卫边角营地"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackRedBase));
 AddRow(War,TEXT("粉色｜蔷薇庭边角营地"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackPinkBase));
 AddSection(War,TEXT("03  可选战利品"));AddRow(War,TEXT("定位南侧每日据点"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackOutpost1));AddRow(War,TEXT("定位北侧每日据点"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,AttackOutpost2));
 War->RemoveFromParent();ToolPages->AddChild(War);
 ResearchActions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ResearchOnlyActions"));ObjectActions->AddChildToVerticalBox(ResearchActions);
 auto* ResearchTabs=WidgetTree->ConstructWidget<UHorizontalBox>();ResearchActions->AddChildToVerticalBox(ResearchTabs);
 auto AddResearchTab=[&](const TCHAR* Caption,FName Function){auto* Button=WidgetTree->ConstructWidget<UButton>();Button->SetContent(Label(WidgetTree,Caption,14,FLinearColor::White));FScriptDelegate D;D.BindUFunction(this,Function);Button->OnClicked.Add(D);auto* Slot=ResearchTabs->AddChildToHorizontalBox(Button);Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Slot->SetPadding(FMargin(2));return Button;};
 UpgradeTab=AddResearchTab(TEXT("永久升级"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,ShowUnitUpgrades));
 ResearchTab=AddResearchTab(TEXT("兵种研究"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,ShowUnitResearch));
 UpgradeList=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("UnitUpgradeList"));ResearchActions->AddChildToVerticalBox(UpgradeList);
 UnlockList=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("UnitResearchList"));ResearchActions->AddChildToVerticalBox(UnlockList);
 AddSection(UpgradeList,TEXT("永久升级 · 提升已有兵种等级"));
 UpgradeList->AddChildToVerticalBox(Label(WidgetTree,TEXT("提升生命、伤害或治疗；保留伤势，新招募部队继承等级。"),12,FLinearColor(.55f,.85f,.76f)));
 SwordButton=AddRow(UpgradeList,TEXT("剑士升级"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,UpgradeSword),&SwordLabel);
 ArcherButton=AddRow(UpgradeList,TEXT("弓手升级"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,UpgradeArcher),&ArcherLabel);
 AddSection(UnlockList,TEXT("兵种研究 · 一次解锁新兵种"));
 UnlockList->AddChildToVerticalBox(Label(WidgetTree,TEXT("消耗材料解锁招募资格，不提升等级；完成后前往对应兵营训练。"),12,FLinearColor(.8f,.65f,1.f)));
 AddRow(UnlockList,TEXT("长枪兵"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,UnlockSpear),&SpearResearchLabel);
 AddRow(UnlockList,TEXT("斥候"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,UnlockScout),&ScoutResearchLabel);
 AddRow(UnlockList,TEXT("牧师"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,UnlockPriest),&PriestResearchLabel);
 CancelTrainingButton=AddRow(ObjectActions,TEXT("取消队尾训练并退款"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,CancelTraining));
 BaseActions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("BaseOnlyActions"));ObjectActions->AddChildToVerticalBox(BaseActions);
 AddSection(BaseActions,TEXT("大本营 · 工人招募"));
 AddRow(BaseActions,TEXT("训练工人｜50粮"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RecruitWorker));
 WorkerActions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WorkerOnlyActions"));ObjectActions->AddChildToVerticalBox(WorkerActions);
 AddSection(WorkerActions,TEXT("工人 · 建造与开发"));
 auto AddBuild=[&](FName Kind,FName Function)
 {
  TObjectPtr<UTextBlock> Text=nullptr;
  WorkerBuildButtons.Add(AddRow(WorkerActions,TEXT("建造 ")+RTSDisplayNames::Entity(Kind).ToString(),Function,&Text,Kind));
  WorkerBuildLabels.Add(Text);WorkerBuildKinds.Add(Kind);
 };
 AddBuild(TEXT("Barracks"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuildFrontlineBarracks));
 AddBuild(TEXT("ArcheryRange"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuildArcheryRange));
 AddBuild(TEXT("ResearchInstitute"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuildResearchInstitute));
 AddBuild(TEXT("SupportBarracks"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuildSupportBarracks));
 AddBuild(TEXT("Farm"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuildFarm));
 AddBuild(TEXT("Tower"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuildTower));
 auto AddFacility=[&](const TCHAR* Name,FName Portrait,FName Function)
 {
  TObjectPtr<UTextBlock> Text=nullptr;
  WorkerFacilityButtons.Add(AddRow(WorkerActions,Name,Function,&Text,Portrait));WorkerFacilityLabels.Add(Text);
 };
 AddFacility(TEXT("住宅 · 扩充人口"),TEXT("Farm"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuyHousing));
 AddFacility(TEXT("建设铁矿前哨"),TEXT("Tower"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuyIronOutpost));
 AddFacility(TEXT("开发富铁矿"),TEXT("ResourceIronOre"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuyRichIron));
 AddFacility(TEXT("建设琥珀前哨"),TEXT("Tower"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuyRefinedOutpost));
 AddFacility(TEXT("开发富琥珀"),TEXT("ResourceAmber"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,BuyRichRefined));
 WorkerBuildHint=Label(WidgetTree,TEXT(""),11,FLinearColor(.72f,.8f,.86f));WorkerActions->AddChildToVerticalBox(WorkerBuildHint)->SetPadding(FMargin(4,2,4,4));
 WorkerActions->SetVisibility(ESlateVisibility::Collapsed);
 MeleeActions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("MeleeOnlyActions"));ObjectActions->AddChildToVerticalBox(MeleeActions);
 AddSection(MeleeActions,TEXT("近战兵营 · 前线部队"));
 AddRow(MeleeActions,TEXT("训练剑士｜20木 50粮"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RecruitSword));
 AddRow(MeleeActions,TEXT("训练长枪兵 · 需研究"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RecruitSpear));
 RangedActions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("RangedOnlyActions"));ObjectActions->AddChildToVerticalBox(RangedActions);
 AddSection(RangedActions,TEXT("远程兵营 · 远程与侦察"));
 AddRow(RangedActions,TEXT("训练弓手｜40木 40粮"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RecruitArcher));
 AddRow(RangedActions,TEXT("训练斥候 · 需研究"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RecruitScout));
 SupportActions=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("SupportOnlyActions"));ObjectActions->AddChildToVerticalBox(SupportActions);
 AddSection(SupportActions,TEXT("后勤兵营 · 战地治疗"));
 AddRow(SupportActions,TEXT("训练牧师 · 需研究"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RecruitPriest));
 auto AddUnitAction=[&](FName Kind,int32 Mode,UVerticalBox* Column)
 {
  auto* Action=NewObject<URTSCampaignAction>(this);Action->Campaign=ARTSCampaign::Get(this);Action->Kind=Kind;Action->Mode=Mode;UnitActions.Add(Action);
  auto* Button=WidgetTree->ConstructWidget<UButton>();ButtonStyle(Button,Accent(1));
  auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Button->SetContent(Row);
  if(auto* ContentSlot=Cast<UButtonSlot>(Row->Slot)){ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);}
  auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(48);Size->SetHeightOverride(48);auto* Image=WidgetTree->ConstructWidget<UImage>();Image->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_Unit_%s"),*Kind.ToString())));Image->SetVisibility(ESlateVisibility::HitTestInvisible);Size->SetContent(Image);auto* IconSlot=Row->AddChildToHorizontalBox(Size);IconSlot->SetPadding(FMargin(0,0,10,0));IconSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));IconSlot->SetVerticalAlignment(VAlign_Center);
  auto* Text=Label(WidgetTree,TEXT(""),13,FLinearColor(.9f,.94f,1));Text->SetJustification(ETextJustify::Left);auto* TextSlot=Row->AddChildToHorizontalBox(Text);TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));TextSlot->SetVerticalAlignment(VAlign_Center);UnitActionLabels.Add(Text);
  Button->OnClicked.AddDynamic(Action,&URTSCampaignAction::Execute);Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3));
 };
 for(int32 I=0;I<RTSCampaignProgression::Count;++I)
 {
  const FName Kind=RTSCampaignProgression::Kind(I);
  if(Kind!=TEXT("Swordman") && Kind!=TEXT("Archer"))AddUnitAction(Kind,1,UpgradeList);
  if(I>=6)
  {
   AddUnitAction(Kind,0,UnlockList);
   const FName Service=ARTSCampaign::TrainingService(Kind);
   AddUnitAction(Kind,2,Service==TEXT("Barracks")?MeleeActions:Service==TEXT("ArcheryRange")?RangedActions:SupportActions);
  }
 }
 MessageText=Label(WidgetTree,TEXT(""),11,FLinearColor(.9f,.72f,.45f));Outer->AddChildToVerticalBox(MessageText)->SetPadding(FMargin(0,12,0,0));
 Dimmer=WidgetTree->ConstructWidget<URTSCampaignInputShield>();Dimmer->SetBrushColor(FLinearColor(0,0,0,.6f));
 auto* DimCell=Canvas->AddChildToCanvas(Dimmer);DimCell->SetAnchors(FAnchors(0,0,1,1));DimCell->SetOffsets(FMargin(0));DimCell->SetZOrder(10);
 GuideCard=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("AdvisorDialogue"));Dark(GuideCard,FLinearColor(.045f,.11f,.14f,.99f));
 auto* GuideCell=Canvas->AddChildToCanvas(GuideCard);GuideCell->SetAnchors(FAnchors(0,1));GuideCell->SetAlignment(FVector2D(0,1));GuideCell->SetPosition(FVector2D(22,-26));GuideCell->SetAutoSize(true);GuideCell->SetZOrder(11);
 auto* GuideRow=WidgetTree->ConstructWidget<UHorizontalBox>();GuideCard->SetContent(GuideRow);
 auto* PortraitSize=WidgetTree->ConstructWidget<USizeBox>();PortraitSize->SetWidthOverride(132);PortraitSize->SetHeightOverride(154);GuideRow->AddChildToHorizontalBox(PortraitSize);
 auto* Portrait=WidgetTree->ConstructWidget<UImage>();PortraitSize->SetContent(Portrait);Portrait->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr,TEXT("/Game/RTS/UI/T_AdvisorElara.T_AdvisorElara")));
 auto* GuideSize=WidgetTree->ConstructWidget<USizeBox>();GuideSize->SetWidthOverride(320);GuideRow->AddChildToHorizontalBox(GuideSize)->SetPadding(FMargin(16,0,0,0));
 auto* GuideColumn=WidgetTree->ConstructWidget<UVerticalBox>();GuideSize->SetContent(GuideColumn);
 GuideColumn->AddChildToVerticalBox(Label(WidgetTree,TEXT("◀  艾拉 · 边境领主"),17,FLinearColor(1,.8f,.4f)));
 GuideText=Label(WidgetTree,TEXT(""),14,FLinearColor(.9f,.97f,.96f));GuideColumn->AddChildToVerticalBox(GuideText)->SetPadding(FMargin(0,12));
 GuideButton=AddRow(GuideColumn,TEXT("明白了 / 继续"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,ConfirmGuide));
 PauseOverlay=WidgetTree->ConstructWidget<URTSCampaignInputShield>();Dark(PauseOverlay,FLinearColor(0,0,0,.78f));
 auto* PauseCell=Canvas->AddChildToCanvas(PauseOverlay);PauseCell->SetAnchors(FAnchors(0,0,1,1));PauseCell->SetOffsets(FMargin(0));PauseCell->SetZOrder(30);
 auto* PauseBox=WidgetTree->ConstructWidget<UVerticalBox>();PauseOverlay->SetContent(PauseBox);PauseOverlay->SetHorizontalAlignment(HAlign_Center);PauseOverlay->SetVerticalAlignment(VAlign_Center);
 PauseBox->AddChildToVerticalBox(Label(WidgetTree,TEXT("游戏已暂停"),30,FLinearColor(1,.85f,.5f)));
 PauseBox->AddChildToVerticalBox(Label(WidgetTree,TEXT("昼夜、战斗、采集和生产已停止"),14,FLinearColor::White))->SetPadding(FMargin(0,16));
 AddRow(PauseBox,TEXT("继续游戏  [P]"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,TogglePause));
 AddRow(PauseBox,TEXT("设置 · 存读档与音量"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,ShowSettings));
 AddRow(PauseBox,TEXT("返回主菜单……"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,RequestReturnToMenu));
 ReturnConfirmation=WidgetTree->ConstructWidget<UVerticalBox>();PauseBox->AddChildToVerticalBox(ReturnConfirmation);
 ReturnConfirmation->AddChildToVerticalBox(Label(WidgetTree,TEXT("返回菜单不会自动保存。未保存的经营和战斗进度将丢失。"),14,FLinearColor(1,.66f,.4f)))->SetPadding(FMargin(0,16));
 ReturnStatus=Label(WidgetTree,TEXT(""),13,FLinearColor(.78f,.92f,.86f));ReturnConfirmation->AddChildToVerticalBox(ReturnStatus)->SetPadding(FMargin(0,6));
 AddRow(ReturnConfirmation,TEXT("保存当前战役并返回主菜单"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,SaveThenReturn));
 AddRow(ReturnConfirmation,TEXT("确认返回，不保存"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,ConfirmReturnToMenu));
 AddRow(ReturnConfirmation,TEXT("取消，留在战场"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,CancelReturnToMenu));
 ReturnConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 PauseOverlay->SetVisibility(ESlateVisibility::Collapsed);
 SettlementOverlay=WidgetTree->ConstructWidget<URTSCampaignInputShield>();Dark(SettlementOverlay,FLinearColor(.015f,.025f,.035f,.9f));
 auto* SettlementCell=Canvas->AddChildToCanvas(SettlementOverlay);SettlementCell->SetAnchors(FAnchors(0,0,1,1));SettlementCell->SetOffsets(FMargin(0));SettlementCell->SetZOrder(50);
 auto* VictorySize=WidgetTree->ConstructWidget<USizeBox>();VictorySize->SetWidthOverride(560);SettlementOverlay->SetContent(VictorySize);SettlementOverlay->SetHorizontalAlignment(HAlign_Center);SettlementOverlay->SetVerticalAlignment(VAlign_Center);
 auto* VictoryFrame=WidgetTree->ConstructWidget<UBorder>();Dark(VictoryFrame,FLinearColor(.065f,.105f,.12f,1));VictoryFrame->SetPadding(FMargin(36,28));VictorySize->SetContent(VictoryFrame);
 auto* SettlementBox=WidgetTree->ConstructWidget<UVerticalBox>();VictoryFrame->SetContent(SettlementBox);
 auto* VictoryKicker=Label(WidgetTree,TEXT("边 境 小 王 国  /  战 役 战 果"),12,FLinearColor(.65f,.76f,.77f));VictoryKicker->SetJustification(ETextJustify::Center);SettlementBox->AddChildToVerticalBox(VictoryKicker)->SetPadding(FMargin(0,0,0,20));
 SettlementTitle=Label(WidgetTree,TEXT(""),38,FLinearColor(1,.83f,.43f));SettlementTitle->SetJustification(ETextJustify::Center);SettlementBox->AddChildToVerticalBox(SettlementTitle);
 auto* VictoryRule=WidgetTree->ConstructWidget<UBorder>();Dark(VictoryRule,FLinearColor(.68f,.53f,.25f,1));auto* RuleSize=WidgetTree->ConstructWidget<USizeBox>();RuleSize->SetHeightOverride(2);VictoryRule->SetContent(RuleSize);SettlementBox->AddChildToVerticalBox(VictoryRule)->SetPadding(FMargin(0,20));
 SettlementBody=Label(WidgetTree,TEXT(""),16,FLinearColor(.84f,.9f,.88f));SettlementBody->SetJustification(ETextJustify::Center);SettlementBody->SetAutoWrapText(true);SettlementBox->AddChildToVerticalBox(SettlementBody)->SetPadding(FMargin(0,0,0,24));
 AddRow(SettlementBox,TEXT("继续经营  ·  守护这片边境"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,KeepCampaignRunning));
 AddRow(SettlementBox,TEXT("保存战果并返回主菜单"),GET_FUNCTION_NAME_CHECKED(URTSCampaignWidget,SettlementSaveAndReturn));
 auto* VictoryNote=Label(WidgetTree,TEXT("部队、库存与永久成长保留。保存仍需等待脱战。"),11,FLinearColor(.57f,.68f,.69f));VictoryNote->SetAutoWrapText(true);VictoryNote->SetJustification(ETextJustify::Center);SettlementBox->AddChildToVerticalBox(VictoryNote)->SetPadding(FMargin(0,16,0,0));
 SettlementOverlay->SetVisibility(ESlateVisibility::Collapsed);
 Settings=CreateWidget<URTSCampaignSettingsWidget>(GetOwningPlayer(),URTSCampaignSettingsWidget::StaticClass(),TEXT("CampaignSettings"));
 auto* SettingsCell=Canvas->AddChildToCanvas(Settings);SettingsCell->SetAnchors(FAnchors(0,0,1,1));SettingsCell->SetOffsets(FMargin(0));SettingsCell->SetZOrder(40);ToolsPanel->SetVisibility(ESlateVisibility::Collapsed);
 Refresh();
}
void URTSCampaignWidget::NativeTick(const FGeometry& Geometry,float DeltaTime)
{
 Super::NativeTick(Geometry,DeltaTime);LabelTimer+=DeltaTime;if(LabelTimer<.3f)return;LabelTimer=0;
 float PanelTop=164.f;
 const FVector2D ScreenSize=Geometry.GetLocalSize();
 if(StockBoard && ScreenSize.X>0 && ScreenSize.Y>0)
 {
  const FGeometry& StockGeometry=StockBoard->GetCachedGeometry();const FVector2D StockMin=Geometry.AbsoluteToLocal(StockGeometry.LocalToAbsolute(FVector2D::ZeroVector));
  const FVector2D StockMax=Geometry.AbsoluteToLocal(StockGeometry.LocalToAbsolute(StockGeometry.GetLocalSize()));
  PanelTop=FMath::Max(164.f,float(StockMax.Y)+12.f);
  if(auto* PC=GetOwningPlayer())if(auto* HUD=Cast<ARTSHUD>(PC->GetHUD()))HUD->SetCampaignStockExclusion(FBox2D((StockMin-FVector2D(6))/ScreenSize,(StockMax+FVector2D(6))/ScreenSize));
 }
 if(auto* BoardSlot=Board?Cast<UCanvasPanelSlot>(Board->Slot):nullptr){FMargin Offsets=BoardSlot->GetOffsets();Offsets.Top=PanelTop;BoardSlot->SetOffsets(Offsets);}
 if(PanelSize && ScreenSize.Y>0)PanelSize->SetHeightOverride(FMath::Max(200.f,float(ScreenSize.Y)-PanelTop-4.f));
 if(ToolsSize && Geometry.GetLocalSize().Y>0)ToolsSize->SetHeightOverride(FMath::Clamp(float(Geometry.GetLocalSize().Y)-192.f,200.f,620.f));
 Refresh();
}
void URTSCampaignWidget::Refresh()
{
 if(PauseOverlay)PauseOverlay->SetVisibility(UGameplayStatics::IsGamePaused(this) && !IsSettingsOpen()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(ReturnConfirmation && !UGameplayStatics::IsGamePaused(this))ReturnConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 auto* C=ARTSCampaign::Get(this);if(!C)return;
 // The settlement card is shown once per end state; it never deletes a save.
 if(SettlementOverlay)
 {
  const bool bWin=C->IsCompleted(),bLose=C->IsDefeated();
  if(!bWin && !bLose){bSettlementShown=false;bSettlementDefeat=false;SettlementOverlay->SetVisibility(ESlateVisibility::Collapsed);}
  if((bWin || bLose) && (!bSettlementShown || bSettlementDefeat!=(bLose && !bWin)))
  {
   bSettlementShown=true;bSettlementDefeat=bLose && !bWin;
   if(SettlementTitle)SettlementTitle->SetText(FText::FromString(bSettlementDefeat?TEXT("边境失守"):TEXT("战役胜利")));
   int32 Army=0,Workers=0;for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->IsAlive() && It->GetTeam()==0){if(It->GetUnitData()->bWorker)++Workers;else ++Army;}
   if(SettlementBody)SettlementBody->SetText(FText::FromString(bSettlementDefeat?TEXT("大本营全部失守，但这并非最后一次远征。\n\n存档未被删除，可读取原记录重新部署。\n失败现场不支持保存，也可以留在战场观战。"):FString::Printf(TEXT("四方大本营尽数陷落，边境重归安宁。\n\n摧毁大本营  4 / 4     ·     战役第 %d 日\n幸存部队  %d     ·     工人  %d\n\n你可以继续建设家园，或保存这场胜利。"),C->GetDay()+1,Army,Workers)));
   SettlementOverlay->SetVisibility(ESlateVisibility::Visible);
  }
 }
 const FString Alert=C->GetAlertText();AlertLabel->SetText(FText::FromString(Alert));AlertBanner->SetVisibility(Alert.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
 const FName Service=C->GetSelectedService();
 SeenEvents=C->GetSeenGuideEvents();
 auto* Controller=Cast<ARTSHumanController>(GetOwningPlayer());
 const TArray<ARTSUnit*> SelectedUnits=Controller && Controller->GetSelection()?Controller->GetSelection()->GetSelectedUnits():TArray<ARTSUnit*>();
 int32 SelectedWorkerCount=0;
 for(const ARTSUnit* Unit:SelectedUnits)if(Unit && Unit->IsAlive() && Unit->GetTeam()==0 && Unit->GetUnitData() && Unit->GetUnitData()->bWorker)++SelectedWorkerCount;
 const bool bWorkerSelection=SelectedWorkerCount>0;
 UpgradeList->SetVisibility(bUnitResearchPage?ESlateVisibility::Collapsed:ESlateVisibility::Visible);UnlockList->SetVisibility(bUnitResearchPage?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 ButtonStyle(UpgradeTab,FLinearColor(.2f,.85f,.65f),!bUnitResearchPage);ButtonStyle(ResearchTab,FLinearColor(.68f,.4f,1.f),bUnitResearchPage);
 const auto* Balance=URTSBalanceSubsystem::Get(this);const auto* Stock=Balance?Balance->GetResources(0):nullptr;
 for(int32 I=0;I<StockLabels.Num();++I)StockLabels[I]->SetText(FText::AsNumber(Stock?Stock->GetAmount(ERTSResource(I)):0));
 int32 People=0,QueuedPeople=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->GetTeam()==0 && It->IsAlive())++People;
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->GetTeam()==0 && It->IsAlive() && It->GetProducer())QueuedPeople+=It->GetProducer()->GetQueue().Num();
 PopulationLabel->SetText(FText::FromString(FString::Printf(TEXT("人口 %d / 最大人口 %d  ·  排队 %d"),People,C->GetPopulationLimit(),QueuedPeople)));
 auto CostColor=[Stock](UTextBlock* Text,const TMap<ERTSResource,int32>& Cost,bool Complete=false){Text->SetColorAndOpacity(Complete?FLinearColor(.72f,.77f,.82f):Stock && Stock->CanAfford(Cost)?FLinearColor(.35f,1.f,.56f):FLinearColor(1.f,.3f,.3f));};
 for(int32 I=0;I<UnitActions.Num();++I)
 {
  const auto* Action=UnitActions[I].Get();FString Text;
  if(Action->Mode==0)Text=C->GetResearchText(Action->Kind);
  else if(Action->Mode==1)Text=C->IsUnitUnlocked(Action->Kind)?C->GetUpgradeText(Action->Kind):RTSCampaignProgression::Name(RTSCampaignProgression::Index(Action->Kind))+TEXT(" · 先研究解锁");
  else {Text=TEXT("训练 ")+RTSCampaignProgression::Name(RTSCampaignProgression::Index(Action->Kind))+TEXT("\n")+RTSCampaignResourceNames::Cost(C->GetTrainingCost(Action->Kind));if(!C->IsUnitUnlocked(Action->Kind))Text+=TEXT(" · 未研究");}
  UnitActionLabels[I]->SetText(FText::FromString(Text));
  const bool Complete=Action->Mode==0?C->IsUnitUnlocked(Action->Kind):Action->Mode==1?(!C->IsUnitUnlocked(Action->Kind) || C->GetLevel(Action->Kind)>=30):false;
  CostColor(UnitActionLabels[I],Action->Mode==0?RTSCampaignProgression::ResearchCost(Action->Kind):Action->Mode==1?RTSCampaignProgression::UpgradeCost(Action->Kind,C->GetLevel(Action->Kind)+1):C->GetTrainingCost(Action->Kind),Complete);
 }
 auto* Resource=Controller?Controller->GetSelectedResource():nullptr;
 ResourceDetails->SetVisibility(Resource?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(Resource)
 {
  const FString PortraitPath=Resource->GetResourcePortraitPath();
  if(PortraitPath!=LastResourcePortrait){LastResourcePortrait=PortraitPath;ResourcePortrait->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr,*PortraitPath));}
  const FString Owner=Resource->GetOwningTeam()==0?TEXT("我方控制 · 可采集"):Resource->GetOwningTeam()==RTSTeam::Neutral?TEXT("中立 · 可直接采集，注意敌军"):TEXT("非我方控制 · 可冒险采集，敌军会攻击工人");
  const FString Slots=Resource->GetGatherSlots()>0?FString::FromInt(Resource->GetGatherSlots()):TEXT("不限");
  ResourceDetailsText->SetText(FText::FromString(FString::Printf(TEXT("%s · %d级\n%s\n储量 %d / %d\n恢复 %.2f / 秒%s\n采集间隔 ×%.2f · 人数 %s"),*Resource->GetResourceDisplayName(),Resource->GetResourceTier(),*Owner,Resource->GetRemaining(),Resource->GetCapacity(),Resource->GetRecoveryPerSecond(),Resource->IsRenewable()?TEXT(""):TEXT("（未启用）"),Resource->GetGatherIntervalMultiplier(),*Slots)));
 }
 ARTSBuilding* Selected=Controller?Controller->GetSelectedBuilding():nullptr;
 if(Selected && !Selected->IsAlive())Selected=nullptr;
 const FName DisplayKind=Selected?Selected->GetBuildingId():NAME_None;
 const bool bWorkerView=bWorkerSelection;
 if(bWorkerView!=bLastWorkerView || Selected!=LastBuilding.Get() || DisplayKind!=LastService)
 {
  bLastWorkerView=bWorkerView;LastBuilding=Selected;LastService=DisplayKind;
  if(bWorkerView)
  {
   BuildingPortrait->SetVisibility(ESlateVisibility::Hidden);
   PageTitle->SetText(FText::FromString(TEXT("工人 · 建造与开发")));
   const int32 WorkerLevel=ARTSCampaign::Get(this)?ARTSCampaign::Get(this)->GetLevel(TEXT("Peasant")):1;
   PageSubtitle->SetText(FText::FromString(FString::Printf(TEXT("选中工人选择建筑；左键空地确认，右键或退格取消。工人6级起在矿点就地入库（当前%d级）。"),WorkerLevel)));
  }
  else
  {
   const FString TexturePath=FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_%s_Portrait.T_%s_Portrait"),*DisplayKind.ToString(),*DisplayKind.ToString());
   BuildingPortrait->SetBrushFromTexture(Selected?LoadObject<UTexture2D>(nullptr,*TexturePath):nullptr);
   BuildingPortrait->SetVisibility(Selected?ESlateVisibility::Visible:ESlateVisibility::Hidden);
   PageTitle->SetText(Selected?RTSDisplayNames::Entity(DisplayKind):FText::FromString(TEXT("未选择建筑")));
   PageSubtitle->SetText(FText::FromString(!Selected?TEXT("点击场景中的建筑，右侧即显示对应图片和指令。"):Selected->GetTeam()!=0?TEXT("敌方建筑 · 不可操作"):DisplayKind==TEXT("ResearchInstitute")?TEXT("研究所专属 · 永久成长与新兵种解锁"):TEXT("当前建筑专属指令")));
  }
  if(PageScroll)PageScroll->SetScrollOffset(0);
  if(bWorkerView)ActivePage=2;
  else if(!Service.IsNone()){ActivePage=Service==TEXT("ResearchInstitute")?1:2;C->NotifyPageOpened(ActivePage);}
 }
 auto Visible=[](UWidget* W,bool Show){W->SetVisibility(Show?ESlateVisibility::Visible:ESlateVisibility::Collapsed);};
 Visible(WorkerActions,bWorkerView);
 Visible(BaseActions,!bWorkerView && Service==TEXT("TownHall"));Visible(MeleeActions,!bWorkerView && Service==TEXT("Barracks"));Visible(RangedActions,!bWorkerView && Service==TEXT("ArcheryRange"));Visible(SupportActions,!bWorkerView && Service==TEXT("SupportBarracks"));Visible(ResearchActions,!bWorkerView && Service==TEXT("ResearchInstitute"));
 FString Context;
 if(bWorkerView)
 {
  Context=FString::Printf(TEXT("已选择%d名工人 · 左键空地确认，右键或退格取消。"),SelectedWorkerCount);
  const bool bPlacing=Controller && Controller->IsPlacingBuilding();
  for(int32 I=0;I<WorkerBuildKinds.Num();++I)
  {
   const FName Kind=WorkerBuildKinds[I];
   const FRTSBalanceRow* Row=Balance?Balance->Find(Kind):nullptr;
   FString Text=TEXT("建造 ")+RTSDisplayNames::Entity(Kind).ToString()+TEXT("\n");
   TMap<ERTSResource,int32> Cost;
   if(Row)
   {
    Cost={{ERTSResource::Wood,Row->WoodCost},{ERTSResource::Food,Row->FoodCost}};
    FString CostText=RTSCampaignResourceNames::Cost(Cost).TrimEnd();
    Text+=FString::Printf(TEXT("%s · %.1f秒"),CostText.IsEmpty()?TEXT("免费"):*CostText,Row->BuildSeconds);
   }
   else Text+=TEXT("数值未就绪");
   WorkerBuildLabels[I]->SetText(FText::FromString(Text));CostColor(WorkerBuildLabels[I],Cost,!Row);
   FString Error;const bool bCanStart=Controller && Controller->CanStartBuildingPlacement(Kind,Error);
   WorkerBuildButtons[I]->SetIsEnabled(bCanStart && !bPlacing);
   WorkerBuildButtons[I]->SetToolTipText(FText::FromString(bPlacing?TEXT("正在放置：左键空地确认，右键或退格取消。"):bCanStart?TEXT("左键空地确认；无效位置不扣资源。选中多名工人可同时施工。"):*Error));
  }
  const TCHAR* FacilityNames[]={TEXT("住宅扩建 +8 · 24→32"),TEXT("建设铁矿前哨"),TEXT("开发富铁矿"),TEXT("建设琥珀前哨"),TEXT("开发富琥珀")};
  for(int32 I=0;I<WorkerFacilityLabels.Num();++I)
  {
   if(I==0 && C->IsFacilityComplete(0))
   {
    const auto Cost=C->PopulationExpansionCost();FString Error;const bool bCanBuy=C->CanBuyPopulation(Error);const bool bMax=C->GetPopulationLimit()>=120;
    const FString Text=bMax?TEXT("资源换人口 · 已达120上限"):FString::Printf(TEXT("资源换人口 +8｜%d → %d\n%s · 立即生效"),C->GetPopulationLimit(),C->GetPopulationLimit()+8,*RTSCampaignResourceNames::Cost(Cost).TrimEnd());
    WorkerFacilityLabels[I]->SetText(FText::FromString(Text));CostColor(WorkerFacilityLabels[I],Cost,bMax);
    WorkerFacilityButtons[I]->SetIsEnabled(bCanBuy && !bPlacing);WorkerFacilityButtons[I]->SetToolTipText(FText::FromString(bCanBuy?TEXT("可重复购买人口名额，费用逐次增加；不直接生成单位。实际招募与队列共用人口上限。"):*Error));
    continue;
   }
   const auto Cost=ARTSCampaign::FacilityCost(I);
   const bool bComplete=C->IsFacilityComplete(I),bPending=C->GetPendingFacility()==I;
   FString Text=FString(FacilityNames[I])+TEXT("\n")+RTSCampaignResourceNames::Cost(Cost).TrimEnd()+FString::Printf(TEXT(" · %.0f秒"),ARTSCampaign::FacilityBuildSeconds(I));
   if(bComplete)Text+=TEXT("\n已完成");
   else if(bPending)Text+=FString::Printf(TEXT("\n建设中 · 剩余%.0f秒"),C->GetFacilitySecondsRemaining());
   WorkerFacilityLabels[I]->SetText(FText::FromString(Text));CostColor(WorkerFacilityLabels[I],Cost,bComplete || bPending);
   WorkerFacilityButtons[I]->SetIsEnabled(!bComplete && C->GetPendingFacility()<0 && !bPlacing);
  }
  WorkerBuildHint->SetText(FText::FromString(bPlacing?TEXT("正在放置：左键空地确认，右键或退格取消。"):TEXT("绿色：资源足够；红色：资源不足。无效建筑位置不扣资源。设施开发需先占领对应矿区。")));
 }
 else
 {
  Context=Selected?FString::Printf(TEXT("生命 %d%% · %s"),FMath::RoundToInt(Selected->GetHealth()->GetHealthPercent()*100),Selected->IsComplete()?TEXT("已建成"):TEXT("施工中")):TEXT("未选择时不显示研究、招募和建设按钮。");
 }
 int32 QueueCount=0;
 auto AddQueueItem=[&](const FString& Name,const FString& Status,const FString& Portrait)
 {
  if(!QueueRows.IsValidIndex(QueueCount))return;
  QueueRows[QueueCount]->SetVisibility(ESlateVisibility::Visible);QueueLabels[QueueCount]->SetText(FText::FromString(Name+TEXT("\n")+Status));
  if(QueuePortraitPaths[QueueCount]!=Portrait){QueuePortraitPaths[QueueCount]=Portrait;QueueImages[QueueCount]->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr,*Portrait));}
  ++QueueCount;
 };
 if(Selected && Selected->GetTeam()==0 && DisplayKind!=TEXT("ResearchInstitute"))if(auto* Producer=Selected->GetProducer())
 {
  const auto& Queue=Producer->GetQueue();
  for(int32 I=0;I<Queue.Num();++I)AddQueueItem(RTSDisplayNames::Entity(Queue[I]).ToString(),I==0?FString::Printf(TEXT("训练中 · %d%% %s"),FMath::RoundToInt(Producer->GetProgress()*100),*Producer->GetSpawnBlockReason()):FString::Printf(TEXT("等待训练 · 第%d位"),I+1),FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_Unit_%s"),*Queue[I].ToString()));
  if(DisplayKind==TEXT("TownHall") && C->GetPendingFacility()>=0)
  {
   const int32 F=C->GetPendingFacility();const TCHAR* Names[]={TEXT("住宅扩建"),TEXT("铁矿前哨"),TEXT("富铁矿开发"),TEXT("琥珀前哨"),TEXT("富琥珀开发")};
   const TCHAR* Images[]={TEXT("Farm"),TEXT("Tower"),TEXT("ResourceIronOre"),TEXT("Tower"),TEXT("ResourceAmber")};
   if(F<5)AddQueueItem(Names[F],FString::Printf(TEXT("建设中 · 剩余%.0f秒"),FMath::Max(0.f,C->GetFacilitySecondsRemaining())),FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_%s_Portrait"),Images[F]));
  }
 }
 for(int32 I=QueueCount;I<QueueRows.Num();++I)QueueRows[I]->SetVisibility(ESlateVisibility::Collapsed);
 QueuePanel->SetVisibility(QueueCount>0?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 ContextText->SetText(FText::FromString(Context));
 Visible(CancelTrainingButton,!bWorkerView && Selected && Selected->GetTeam()==0 && Selected->GetProducer() && Selected->GetProducer()->GetQueue().Num()>0);
 OverviewText->SetText(FText::FromString(C->GetOverview()));ObjectivesText->SetText(FText::FromString(C->GetObjectives()));WarText->SetText(FText::FromString(C->GetWarPlan()));
 MessageText->SetText(FText::FromString(C->GetMessage().Left(100)));SwordLabel->SetText(FText::FromString(C->GetUpgradeText(TEXT("Swordman"))));ArcherLabel->SetText(FText::FromString(C->GetUpgradeText(TEXT("Archer"))));
 SpearResearchLabel->SetText(FText::FromString(C->GetResearchText(TEXT("Spearman"))));ScoutResearchLabel->SetText(FText::FromString(C->GetResearchText(TEXT("Scout"))));PriestResearchLabel->SetText(FText::FromString(C->GetResearchText(TEXT("Priest"))));
 CostColor(SwordLabel,RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),C->GetLevel(TEXT("Swordman"))+1),C->GetLevel(TEXT("Swordman"))>=30);
 CostColor(ArcherLabel,RTSCampaignProgression::UpgradeCost(TEXT("Archer"),C->GetLevel(TEXT("Archer"))+1),C->GetLevel(TEXT("Archer"))>=30);
 CostColor(SpearResearchLabel,RTSCampaignProgression::ResearchCost(TEXT("Spearman")),C->IsUnitUnlocked(TEXT("Spearman")));
 CostColor(ScoutResearchLabel,RTSCampaignProgression::ResearchCost(TEXT("Scout")),C->IsUnitUnlocked(TEXT("Scout")));
 CostColor(PriestResearchLabel,RTSCampaignProgression::ResearchCost(TEXT("Priest")),C->IsUnitUnlocked(TEXT("Priest")));
 if(C->GetGuideStep()>=7 && (EventDialogue.IsEmpty() || (!(SeenEvents&1) && C->GetActiveRaidCount()>0)))
 {
  if(!(SeenEvents&1) && C->GetActiveRaidCount()>0){SeenEvents|=1;EventDialogue=TEXT("敌军开始集结了！\n先看右侧战报和小地图，保护采集线。你可以选择迎击，也可以收拢工人；不必立刻挑战敌方大营。");}
  else if(!(SeenEvents&2) && C->GetRegionMask()!=0){SeenEvents|=2;EventDialogue=TEXT("我们拿下了新的资源区！\n先在主基地安排前哨，再命令工人实际开采。占领本身不会自动替你采集资源。");}
  else if(!(SeenEvents&4) && (C->IsUnitUnlocked(TEXT("Spearman")) || C->IsUnitUnlocked(TEXT("Scout")) || C->IsUnitUnlocked(TEXT("Priest")))){SeenEvents|=4;EventDialogue=TEXT("新兵种研究完成！\n长枪兵去近战兵营，斥候去远程兵营，牧师去后勤兵营。牧师负责治疗，不会攻击敌人。");}
 }
 const bool Tutorial=C->GetGuideStep()<7;
 const bool Modal=!EventDialogue.IsEmpty() || C->GetGuideStep()==0 || (C->GetGuideStep()==6 && ToolsPanel->GetVisibility()==ESlateVisibility::Visible && ToolPages->GetActiveWidgetIndex()==0);
 GuideText->SetText(FText::FromString(EventDialogue.IsEmpty()?C->GetGuidance():EventDialogue));
 C->MarkGuideEvent(SeenEvents);
 Visible(GuideCard,Tutorial || !EventDialogue.IsEmpty());Visible(GuideButton,Modal);
 Dimmer->SetVisibility(Modal?ESlateVisibility::Visible:Tutorial?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 Dimmer->SetBrushColor(FLinearColor(0,0,0,Modal?.6f:.12f));
}
void URTSCampaignWidget::ConfirmGuide(){if(!EventDialogue.IsEmpty())EventDialogue.Empty();else if(auto* C=ARTSCampaign::Get(this))C->AdvanceGuide();Refresh();}
void URTSCampaignWidget::ResetMinimap(){if(CampaignMinimap)CampaignMinimap->ResetMapView();}
void URTSCampaignWidget::ShowCampaignPage() { SelectPage(0); }
void URTSCampaignWidget::ShowGrowthPage() { SelectPage(1); }
void URTSCampaignWidget::ShowProductionPage() { SelectPage(2); }
void URTSCampaignWidget::ShowSystemPage() { ShowSettings(); }
void URTSCampaignWidget::UpgradeSword()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->Upgrade(TEXT("Swordman")); }
	Refresh();
}
void URTSCampaignWidget::UpgradeArcher()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->Upgrade(TEXT("Archer")); }
	Refresh();
}
void URTSCampaignWidget::AssignWood()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AssignWorkers(0); }
	Refresh();
}
void URTSCampaignWidget::AssignFood()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AssignWorkers(1); }
	Refresh();
}
void URTSCampaignWidget::AssignIron()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AssignWorkers(2); }
	Refresh();
}
void URTSCampaignWidget::AssignRefined()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AssignWorkers(3); }
	Refresh();
}
void URTSCampaignWidget::AttackIron()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(0); }
	Refresh();
}
void URTSCampaignWidget::AttackRichIron()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(1); }
	Refresh();
}
void URTSCampaignWidget::AttackRefined()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(2); }
	Refresh();
}
void URTSCampaignWidget::AttackRichRefined()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(3); }
	Refresh();
}
void URTSCampaignWidget::AttackOutpost1()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(4); }
	Refresh();
}
void URTSCampaignWidget::AttackOutpost2()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(5); }
	Refresh();
}
void URTSCampaignWidget::BuyHousing()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { if(Campaign->IsFacilityComplete(0))Campaign->BuyPopulation();else Campaign->BuyFacility(0); }
	Refresh();
}
void URTSCampaignWidget::BuyIronOutpost()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->BuyFacility(1); }
	Refresh();
}
void URTSCampaignWidget::BuyRichIron()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->BuyFacility(2); }
	Refresh();
}
void URTSCampaignWidget::BuyRefinedOutpost()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->BuyFacility(3); }
	Refresh();
}
void URTSCampaignWidget::BuyRichRefined()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->BuyFacility(4); }
	Refresh();
}
void URTSCampaignWidget::RecruitWorker()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->Train(TEXT("Peasant")); }
	Refresh();
}
void URTSCampaignWidget::RecruitSword()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->Train(TEXT("Swordman")); }
	Refresh();
}
void URTSCampaignWidget::RecruitArcher()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->Train(TEXT("Archer")); }
	Refresh();
}
void URTSCampaignWidget::FocusHome()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->FocusHome(); }
	Refresh();
}
void URTSCampaignWidget::PrepareSave()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->PrepareSave(); }
	Refresh();
}
void URTSCampaignWidget::Save()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->SaveCampaign(); }
	Refresh();
}
void URTSCampaignWidget::Load()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->LoadCampaign(); }
	Refresh();
}
void URTSCampaignWidget::AttackLord()
{
	if (ARTSCampaign* Campaign = ARTSCampaign::Get(this)) { Campaign->AttackObjective(6); }
	Refresh();
}
void URTSCampaignWidget::SelectBase(){if(auto* C=ARTSCampaign::Get(this))C->SelectServiceBuilding(TEXT("TownHall"));SelectPage(2);Refresh();}
void URTSCampaignWidget::SelectMelee(){if(auto* C=ARTSCampaign::Get(this))C->SelectServiceBuilding(TEXT("Barracks"));SelectPage(2);Refresh();}
void URTSCampaignWidget::SelectRanged(){if(auto* C=ARTSCampaign::Get(this))C->SelectServiceBuilding(TEXT("ArcheryRange"));SelectPage(2);Refresh();}
void URTSCampaignWidget::SelectResearch(){if(auto* C=ARTSCampaign::Get(this))C->SelectServiceBuilding(TEXT("ResearchInstitute"));SelectPage(1);Refresh();}
void URTSCampaignWidget::SelectSupport(){if(auto* C=ARTSCampaign::Get(this))C->SelectServiceBuilding(TEXT("SupportBarracks"));SelectPage(2);Refresh();}
void URTSCampaignWidget::UnlockSpear(){if(auto* C=ARTSCampaign::Get(this))C->ResearchUnit(TEXT("Spearman"));Refresh();}
void URTSCampaignWidget::UnlockScout(){if(auto* C=ARTSCampaign::Get(this))C->ResearchUnit(TEXT("Scout"));Refresh();}
void URTSCampaignWidget::UnlockPriest(){if(auto* C=ARTSCampaign::Get(this))C->ResearchUnit(TEXT("Priest"));Refresh();}
void URTSCampaignWidget::RecruitSpear(){if(auto* C=ARTSCampaign::Get(this))C->Train(TEXT("Spearman"));Refresh();}
void URTSCampaignWidget::RecruitScout(){if(auto* C=ARTSCampaign::Get(this))C->Train(TEXT("Scout"));Refresh();}
void URTSCampaignWidget::RecruitPriest(){if(auto* C=ARTSCampaign::Get(this))C->Train(TEXT("Priest"));Refresh();}
void URTSCampaignWidget::CycleHighlandColor(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(0);Refresh();}
void URTSCampaignWidget::CycleValleyColor(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(1);Refresh();}
void URTSCampaignWidget::CancelTraining()
{
 if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))
 {
  if(auto* Building=PC->GetSelectedBuilding())
  {
   if(Building->GetTeam()==0 && Building->IsAlive())
   {
    if(auto* Producer=Building->GetProducer())Producer->CancelLast();
   }
  }
 }
 Refresh();
}
void URTSCampaignWidget::BuildFrontlineBarracks()
{
 if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->BeginBuildingPlacement(TEXT("Barracks"));
 Refresh();
}
void URTSCampaignWidget::BuildArcheryRange(){if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->BeginBuildingPlacement(TEXT("ArcheryRange"));Refresh();}
void URTSCampaignWidget::BuildResearchInstitute(){if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->BeginBuildingPlacement(TEXT("ResearchInstitute"));Refresh();}
void URTSCampaignWidget::BuildSupportBarracks(){if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->BeginBuildingPlacement(TEXT("SupportBarracks"));Refresh();}
void URTSCampaignWidget::BuildFarm(){if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->BeginBuildingPlacement(TEXT("Farm"));Refresh();}
void URTSCampaignWidget::BuildTower(){if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->BeginBuildingPlacement(TEXT("Tower"));Refresh();}
void URTSCampaignWidget::CloseTools(){if(ToolsPanel)ToolsPanel->SetVisibility(ESlateVisibility::Collapsed);}
void URTSCampaignWidget::ShowSettings(){CloseTools();if(Settings)Settings->OpenSettings();Refresh();}
bool URTSCampaignWidget::IsSettingsOpen() const { return Settings && Settings->IsOpen(); }
bool URTSCampaignWidget::HasBuildingPortrait() const { return BuildingPortrait && BuildingPortrait->GetBrush().GetResourceObject()!=nullptr; }
bool URTSCampaignWidget::IsOnlyServiceVisible(FName Kind) const
{
 const FName Kinds[]={TEXT("TownHall"),TEXT("Barracks"),TEXT("ArcheryRange"),TEXT("ResearchInstitute"),TEXT("SupportBarracks")};
 const UVerticalBox* Boxes[]={BaseActions,MeleeActions,RangedActions,ResearchActions,SupportActions};
 for(int32 I=0;I<5;++I)if((Boxes[I]->GetVisibility()==ESlateVisibility::Visible)!=(Kinds[I]==Kind))return false;
 return true;
}
void URTSCampaignWidget::AttackRedBase(){if(auto* C=ARTSCampaign::Get(this))C->AttackObjective(7);}
void URTSCampaignWidget::AttackPinkBase(){if(auto* C=ARTSCampaign::Get(this))C->AttackObjective(8);}
void URTSCampaignWidget::CycleRedColor(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(2);Refresh();}
void URTSCampaignWidget::CyclePinkColor(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(3);Refresh();}
bool URTSCampaignWidget::IsResourceDetailsVisible() const
{
 return ResourceDetails && ResourceDetails->GetVisibility()==ESlateVisibility::Visible && ResourcePortrait && ResourcePortrait->GetBrush().GetResourceObject()!=nullptr && ResourceDetailsText && !ResourceDetailsText->GetText().IsEmpty();
}
void URTSCampaignWidget::TogglePause()
{
 if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))PC->ToggleGamePause();
 Refresh();
}
bool URTSCampaignWidget::IsPauseMenuVisible() const
{return PauseOverlay && PauseOverlay->GetVisibility()==ESlateVisibility::Visible;}
void URTSCampaignWidget::ShowUnitUpgrades()
{ bUnitResearchPage=false;if(PageScroll)PageScroll->SetScrollOffset(0);Refresh(); }
void URTSCampaignWidget::ShowUnitResearch()
{ bUnitResearchPage=true;if(PageScroll)PageScroll->SetScrollOffset(0);Refresh(); }
void URTSCampaignWidget::RequestReturnToMenu() { if(ReturnConfirmation)ReturnConfirmation->SetVisibility(ESlateVisibility::Visible); }
void URTSCampaignWidget::CancelReturnToMenu() { if(ReturnConfirmation)ReturnConfirmation->SetVisibility(ESlateVisibility::Collapsed); }
void URTSCampaignWidget::ConfirmReturnToMenu()
{
 if(ReturnConfirmation && ReturnConfirmation->GetVisibility()==ESlateVisibility::Visible)RTSCampaignEntry::OpenMenu(this);
}

void URTSCampaignWidget::SaveThenReturn()
{
 auto* C=ARTSCampaign::Get(this);
 if(!C){ if(ReturnStatus)ReturnStatus->SetText(FText::FromString(TEXT("当前没有可保存的战役。"))); return; }
 if(!C->SaveCampaign())
 {
  if(ReturnStatus)ReturnStatus->SetText(FText::FromString(FString(TEXT("未保存："))+C->GetMessage()));
  return;
 }
 if(ReturnStatus)ReturnStatus->SetText(FText::FromString(TEXT("已保存，正在返回主菜单。")));
 ConfirmReturnToMenu();
}

void URTSCampaignWidget::KeepCampaignRunning()
{
 // Dismissing the card never changes campaign state: it only lets the player keep looking around.
 if(SettlementOverlay)SettlementOverlay->SetVisibility(ESlateVisibility::Collapsed);
}
void URTSCampaignWidget::SettlementSaveAndReturn()
{
 auto* C=ARTSCampaign::Get(this);
 if(!C || !C->IsCompleted()) { RTSCampaignEntry::OpenMenu(this); return; }
 if(!C->SaveCampaign())
 {
  if(SettlementBody)SettlementBody->SetText(FText::FromString(FString(TEXT("未保存："))+C->GetMessage()+TEXT("\n脱战后可再次尝试；仍未离开战场。")));
  return;
 }
 RTSCampaignEntry::OpenMenu(this);
}
