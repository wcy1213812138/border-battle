#include "Campaign/RTSCampaignEntry.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Campaign/RTSCampaignCookAssets.h"
#include "Campaign/RTSCampaignInputShield.h"
#include "UI/RTSUIFont.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"

namespace RTSCampaignEntry
{
 const TCHAR* MenuMap() { return TEXT("/Game/RTS/Maps/L_CampaignMenu"); }
 const TCHAR* CampaignMap() { return TEXT("/Game/RTS/Maps/L_BorderIncremental"); }
 bool IsContinueRequest(const UObject* Context)
 {
  const UWorld* World=Context?Context->GetWorld():nullptr;
  return World && World->URL.HasOption(TEXT("CampaignContinue"));
 }
 void OpenMenu(const UObject* Context)
 {
  UGameplayStatics::SetGamePaused(Context,false);
  UGameplayStatics::OpenLevel(Context,FName(MenuMap()),true);
 }
}

ARTSCampaignMenuGameMode::ARTSCampaignMenuGameMode()
{
 CookAssets=GetMutableDefault<URTSCampaignCookAssets>();
 DefaultPawnClass=nullptr;
 HUDClass=nullptr;
 PlayerControllerClass=APlayerController::StaticClass();
 bStartPlayersAsSpectators=true;
}
void ARTSCampaignMenuGameMode::BeginPlay()
{
 Super::BeginPlay();
 if(APlayerController* PC=GetWorld()->GetFirstPlayerController())
  if(auto* Menu=CreateWidget<URTSCampaignEntryWidget>(PC,URTSCampaignEntryWidget::StaticClass()))Menu->AddToViewport(100);
}

namespace
{
 UTextBlock* EntryText(UWidgetTree* Tree,const FString& Caption,int32 Size,FLinearColor Color)
 {
  auto* Text=Tree->ConstructWidget<UTextBlock>();
  Text->SetText(FText::FromString(Caption));Text->SetFont(RTSUIFont::Chinese(Size));
  Text->SetColorAndOpacity(Color);Text->SetAutoWrapText(true);
  return Text;
 }
}
UButton* URTSCampaignEntryWidget::AddButton(UVerticalBox* Column,const TCHAR* Name,const FString& Caption,FName Function)
{
 auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(Name));
 Button->SetBackgroundColor(FLinearColor(.09f,.25f,.28f,1));
 auto* Text=EntryText(WidgetTree,Caption,20,FLinearColor(.95f,.96f,.9f));
 Text->SetAutoWrapText(false);Text->SetJustification(ETextJustify::Center);
 Button->AddChild(Text);CastChecked<UButtonSlot>(Text->Slot)->SetPadding(FMargin(20,12));
 FScriptDelegate Click;Click.BindUFunction(this,Function);Button->OnClicked.Add(Click);
 Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,5));
 return Button;
}
void URTSCampaignEntryWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();SetIsFocusable(true);
 bLoading=Cast<ARTSCampaignGameMode>(GetWorld()->GetAuthGameMode())!=nullptr;
 StartupBegan=FPlatformTime::Seconds();
 auto* Root=WidgetTree->ConstructWidget<URTSCampaignInputShield>();WidgetTree->RootWidget=Root;
 Root->SetBrushColor(FLinearColor(.014f,.028f,.036f,1));Root->SetHorizontalAlignment(HAlign_Center);Root->SetVerticalAlignment(VAlign_Center);
 auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(600);Root->SetContent(Size);
 auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Size->SetContent(Column);
 Column->AddChildToVerticalBox(EntryText(WidgetTree,TEXT("边境小王国"),42,FLinearColor(1,.83f,.47f)))->SetPadding(FMargin(0,0,0,12));
 Column->AddChildToVerticalBox(EntryText(WidgetTree,TEXT("单人持续战役 · 经营 / 永久成长 / 资源争夺"),17,FLinearColor(.65f,.81f,.8f)))->SetPadding(FMargin(0,0,0,22));
 Status=EntryText(WidgetTree,bLoading?TEXT("正在准备战场与导航，请稍候……"):ARTSCampaign::HasCampaignSave()?TEXT("发现本机战役记录。可以继续，或开始一段新的征程。"):TEXT("尚无本机战役记录。开始新战役，跟随艾拉学习经营与战斗。"),17,FLinearColor(.83f,.88f,.87f));
 Column->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,0,0,18));
 MenuActions=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(MenuActions);
 ContinueButton=AddButton(MenuActions,TEXT("ContinueCampaign"),TEXT("继续战役"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,ContinueCampaign));
 ContinueButton->SetIsEnabled(ARTSCampaign::HasSlot(ARTSCampaign::ActiveSlot()));
 NewButton=AddButton(MenuActions,TEXT("NewCampaign"),TEXT("新战役"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,NewCampaign));
 AddButton(MenuActions,TEXT("Quit"),TEXT("退出游戏"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,Quit));
 BuildSlotRows(Column);
 MenuActions->SetVisibility(bLoading?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
 Confirmation=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(Confirmation);Confirmation->SetVisibility(ESlateVisibility::Collapsed);
 AddButton(Confirmation,TEXT("ConfirmNewCampaign"),TEXT("确认开始新战役"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,ConfirmNewCampaign));
 AddButton(Confirmation,TEXT("CancelNewCampaign"),TEXT("取消，返回"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,CancelNewCampaign));
 Recovery=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(Recovery);Recovery->SetVisibility(bLoading?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 AddButton(Recovery,TEXT("ReturnToMenu"),TEXT("返回主菜单"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,ReturnToMenu));
 AddButton(Recovery,TEXT("QuitFromLoading"),TEXT("退出游戏"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,Quit));
 Column->AddChildToVerticalBox(EntryText(WidgetTree,TEXT("边境战役 · 引导完成后开放手动保存\n脱战后可在经营中手动保存；返回菜单不会自动保存。"),13,FLinearColor(.56f,.67f,.7f)))->SetPadding(FMargin(0,20,0,0));
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ENTRY %s save=%d"),bLoading?TEXT("loading"):TEXT("menu"),ARTSCampaign::HasCampaignSave());
}
void URTSCampaignEntryWidget::NativeConstruct()
{
 Super::NativeConstruct();
 if(auto* PC=GetOwningPlayer())
 {
  PC->bShowMouseCursor=true;
  FInputModeUIOnly Mode;Mode.SetWidgetToFocus(TakeWidget());Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
 }
 // Loading and its watchdog must progress even when Slate skips an occluded window.
 if(bLoading)
 {
  TWeakObjectPtr<URTSCampaignEntryWidget> WeakThis(this);
  StartupTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
  {
   auto* Widget=WeakThis.Get();if(!Widget || !Widget->IsInViewport())return false;
   Widget->UpdateStartup();return !Widget->bFailed && !Widget->bTransitioning;
  }),.1f);
 }
}
void URTSCampaignEntryWidget::NativeDestruct()
{
 FTSTicker::RemoveTicker(StartupTicker);StartupTicker.Reset();Super::NativeDestruct();
}
void URTSCampaignEntryWidget::ShowError(const FString& Error)
{
 bFailed=true;Status->SetText(FText::FromString(Error+TEXT("\n原存档未被覆盖。请返回主菜单后重试，或开始新战役。")));
 UGameplayStatics::SetGamePaused(this,true);
 UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_ENTRY failed: %s"),*Error);
}
void URTSCampaignEntryWidget::UpdateStartup()
{
 if(!bLoading || bFailed || bTransitioning)return;
 auto* Campaign=ARTSCampaign::Get(this);
 if(!Campaign || !Campaign->IsReady())
 {
  if(FPlatformTime::Seconds()-StartupBegan>180)ShowError(TEXT("战场导航准备超过180秒，未能进入战役。"));
  return;
 }
 if(RTSCampaignEntry::IsContinueRequest(this) && !Campaign->LoadCampaignOnEntry()) { ShowError(Campaign->GetMessage());return; }
 TArray<UUserWidget*> CampaignWidgets;
 UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,CampaignWidgets,URTSCampaignWidget::StaticClass(),true);
 for(auto* Widget:CampaignWidgets)CastChecked<URTSCampaignWidget>(Widget)->RefreshSelectedObject();
 if(auto* PC=GetOwningPlayer())
 {
  FInputModeGameAndUI Mode;Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);Mode.SetHideCursorDuringCapture(false);PC->SetInputMode(Mode);
 }
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ENTRY ready continue=%d"),RTSCampaignEntry::IsContinueRequest(this));
 bTransitioning=true;RemoveFromParent();
}
void URTSCampaignEntryWidget::OpenCampaign(bool bContinue)
{
 if(bLoading || bTransitioning)return;
 bTransitioning=true;MenuActions->SetIsEnabled(false);Confirmation->SetIsEnabled(false);
 Status->SetText(FText::FromString(TEXT("正在打开边境战场……")));
 UGameplayStatics::OpenLevel(this,FName(RTSCampaignEntry::CampaignMap()),true,bContinue?TEXT("CampaignContinue"):TEXT(""));
}
void URTSCampaignEntryWidget::ContinueCampaign()
{
 if(!bLoading && !bConfirming && PendingCleanSlot<0 && ARTSCampaign::HasSlot(ARTSCampaign::ActiveSlot()))OpenCampaign(true);
}
void URTSCampaignEntryWidget::NewCampaign()
{
 if(bLoading || bTransitioning)return;
 bConfirming=true;MenuActions->SetVisibility(ESlateVisibility::Collapsed);Confirmation->SetVisibility(ESlateVisibility::Visible);
 Status->SetText(FText::FromString(TEXT("确认开始新战役？\n旧记录不会立即删除或覆盖。新战役的下一次手动保存会替换同一正式存档槽。")));
}
void URTSCampaignEntryWidget::ConfirmNewCampaign() { if(bConfirming)OpenCampaign(false); }
void URTSCampaignEntryWidget::CancelNewCampaign()
{
 if(bTransitioning)return;
 bConfirming=false;Confirmation->SetVisibility(ESlateVisibility::Collapsed);MenuActions->SetVisibility(ESlateVisibility::Visible);
 Status->SetText(FText::FromString(TEXT("已取消。原有战役记录保持不变。")));if(NewButton)NewButton->SetKeyboardFocus();
}
void URTSCampaignEntryWidget::ReturnToMenu() { bTransitioning=true;RTSCampaignEntry::OpenMenu(this); }
void URTSCampaignEntryWidget::Quit() { UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false); }
FReply URTSCampaignEntryWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
 if(Event.GetKey()==EKeys::Escape && bConfirming){CancelNewCampaign();return FReply::Handled();}
 if(Event.GetKey()==EKeys::Escape && PendingCleanSlot>=0){CancelCleanSlot();return FReply::Handled();}
 return Super::NativeOnKeyDown(Geometry,Event);
}

void URTSCampaignEntryWidget::BuildSlotRows(UVerticalBox* Column)
{
 SlotSection=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CampaignSlots"));
 Column->AddChildToVerticalBox(SlotSection);
 SlotSection->AddChildToVerticalBox(EntryText(WidgetTree,TEXT("存档记录"),16,FLinearColor(.78f,.9f,.86f)))->SetPadding(FMargin(0,16,0,4));
 SlotSection->AddChildToVerticalBox(EntryText(WidgetTree,TEXT("左键选择存档，再点上方“继续战役”；右键清理，清理前需要确认。"),12,FLinearColor(.6f,.71f,.73f)))->SetPadding(FMargin(0,0,0,4));
 SlotRows=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CampaignSlotRows"));SlotSection->AddChildToVerticalBox(SlotRows);
 if(APlayerController* PC=GetOwningPlayer())
 {
  for(int32 Index=0;Index<3;++Index)
  {
   auto* Row=CreateWidget<URTSCampaignSaveSlotRowWidget>(PC,URTSCampaignSaveSlotRowWidget::StaticClass());
   if(!Row)continue;
   Row->Setup(Index,this);
   SlotRowWidgets.Add(Row);SlotRows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,2));
  }
 }
 CleanConfirmation=WidgetTree->ConstructWidget<UVerticalBox>();SlotSection->AddChildToVerticalBox(CleanConfirmation);
 AddButton(CleanConfirmation,TEXT("ConfirmCleanSlot"),TEXT("确认清理该槽（不可撤销）"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,ConfirmCleanSlot));
 AddButton(CleanConfirmation,TEXT("CancelCleanSlot"),TEXT("取消清理"),GET_FUNCTION_NAME_CHECKED(URTSCampaignEntryWidget,CancelCleanSlot));
 CleanStatus=EntryText(WidgetTree,TEXT(""),13,FLinearColor(1,.8f,.48f));
 CleanConfirmation->AddChildToVerticalBox(CleanStatus)->SetPadding(FMargin(0,6));
 CleanConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 SlotSection->SetVisibility(bLoading?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
}
void URTSCampaignEntryWidget::RefreshSlotRows()
{
 for(auto& Row:SlotRowWidgets) if(Row) Row->Setup(Row->GetSlotIndex(),this);
}
void URTSCampaignEntryWidget::RequestLoadSlot(int32 Index)
{
 if(bLoading || bTransitioning || bConfirming || Index<0 || Index>2)return;
 PendingCleanSlot=-1;
 if(CleanConfirmation)CleanConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 ARTSCampaign::SetActiveSlot(Index);
 RefreshSlotRows();
 const bool bExists=ARTSCampaign::HasSlot(Index);
 if(ContinueButton)ContinueButton->SetIsEnabled(bExists);
 const FString SelectionText=bExists?FString::Printf(TEXT("已选择槽 %d，点击上方“继续战役”进入。"),Index+1):FString::Printf(TEXT("已选择槽 %d，此槽为空。请选择其他存档或开始新战役。"),Index+1);
 Status->SetText(FText::FromString(SelectionText));
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ENTRY selected slot=%d exists=%d"),Index+1,bExists);
}
void URTSCampaignEntryWidget::RequestCleanSlot(int32 Index)
{
 if(bLoading || bTransitioning || !CleanConfirmation)return;
 if(PendingCleanSlot>=0 && PendingCleanSlot!=Index)return;
 ARTSCampaign::FRTSSlotInfo Info;ARTSCampaign::DescribeSlot(Index,Info);
 if(!Info.bExists)
 {
  Status->SetText(FText::FromString(FString::Printf(TEXT("槽 %d 是空的，没有可清理的记录。"),Index+1)));
  return;
 }
 PendingCleanSlot=Index;CleanConfirmation->SetVisibility(ESlateVisibility::Visible);
 if(CleanStatus)CleanStatus->SetText(FText::FromString(FString::Printf(TEXT("将删除槽 %d 的记录与它自己的上一份备份%s。此操作不可撤销。"),Index+1,Index<=0?TEXT("（含自动存档与兼容备份）"):TEXT(""))));
 Status->SetText(FText::FromString(TEXT("已选中要清理的存档行；按下确认才会真正删除。")));
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ENTRY clean_request slot=%d"),Index+1);
}
void URTSCampaignEntryWidget::ConfirmCleanSlot()
{
 if(PendingCleanSlot<0 || bTransitioning || bLoading || !CleanConfirmation)return;
 const int32 Index=PendingCleanSlot;PendingCleanSlot=-1;
 FString Summary;const bool bDeleted=ARTSCampaign::DeleteSlotRecords(Index,Summary);
 CleanConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(ContinueButton)ContinueButton->SetIsEnabled(ARTSCampaign::HasSlot(ARTSCampaign::ActiveSlot()));
 RefreshSlotRows();
 Status->SetText(FText::FromString(bDeleted?Summary:FString::Printf(TEXT("槽 %d 本来就是空的，没有删除任何文件。"),Index+1)));
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ENTRY clean slot=%d deleted=%d"),Index+1,bDeleted?1:0);
}
void URTSCampaignEntryWidget::CancelCleanSlot()
{
 if(bTransitioning)return;
 PendingCleanSlot=-1;
 if(CleanConfirmation)CleanConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 Status->SetText(FText::FromString(TEXT("已取消清理，磁盘记录保持不变。")));
}

void URTSCampaignSaveSlotRowWidget::Setup(int32 InIndex,URTSCampaignEntryWidget* InOwner)
{
 Index=InIndex;Owner=InOwner;
 if(!WidgetTree)return;
 if(!Frame)
 {
  Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CampaignSlotFrame"));
  Frame->SetHorizontalAlignment(HAlign_Fill);Frame->SetVerticalAlignment(VAlign_Center);Frame->SetPadding(FMargin(12,8));
  WidgetTree->RootWidget=Frame;
  Caption=WidgetTree->ConstructWidget<UTextBlock>();
  Caption->SetFont(RTSUIFont::Chinese(15));
  Frame->SetContent(Caption);
 }
 SetVisibility(ESlateVisibility::Visible);
 Refresh();
}
void URTSCampaignSaveSlotRowWidget::Refresh()
{
 if(!Caption)return;
 ARTSCampaign::FRTSSlotInfo Info;ARTSCampaign::DescribeSlot(Index,Info);
 const TCHAR* Kind=Index<=0?TEXT("主档"):TEXT("手动档");
 const bool bSelected=Index==ARTSCampaign::ActiveSlot();
 if(Info.bExists)
 {
  const FString Size=Info.Size>=1024?FString::Printf(TEXT("%.1f KB"),Info.Size/1024.0):FString::Printf(TEXT("%d B"),int32(Info.Size));
  Caption->SetText(FText::FromString(FString::Printf(TEXT("%s槽 %d · %s · %s · 保存于 %s%s"),bSelected?TEXT("▶ "):TEXT(""),Index+1,Kind,*Size,*Info.Modified.ToString(TEXT("%Y-%m-%d %H:%M")),bHovered?TEXT("　—　左键选择 · 右键清理"):TEXT(""))));
  Caption->SetColorAndOpacity(bSelected?FLinearColor(1,.86f,.5f):FLinearColor(.86f,.95f,.92f));
 }
 else
 {
  Caption->SetText(FText::FromString(FString::Printf(TEXT("%s槽 %d · %s · 空"),bSelected?TEXT("▶ "):TEXT(""),Index+1,Kind)));
  Caption->SetColorAndOpacity(FLinearColor(.5f,.58f,.6f));
 }
 if(Frame)Frame->SetBrushColor(bSelected?FLinearColor(.16f,.35f,.29f,1):bHovered?FLinearColor(.12f,.33f,.36f,1):FLinearColor(.06f,.15f,.18f,1));
}
void URTSCampaignSaveSlotRowWidget::Paint(bool bInHovered)
{
 if(bHovered==bInHovered)return;
 bHovered=bInHovered;Refresh();
}
FReply URTSCampaignSaveSlotRowWidget::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
 if(Event.GetEffectingButton()==EKeys::RightMouseButton)
 {
  if(Owner)Owner->RequestCleanSlot(Index);
  return FReply::Handled();
 }
 if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
 {
  if(Owner)Owner->RequestLoadSlot(Index);
  return FReply::Handled();
 }
 return Super::NativeOnMouseButtonDown(Geometry,Event);
}
void URTSCampaignSaveSlotRowWidget::NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event)
{
 Super::NativeOnMouseEnter(Geometry,Event);Paint(true);
}
void URTSCampaignSaveSlotRowWidget::NativeOnMouseLeave(const FPointerEvent& Event)
{
 Super::NativeOnMouseLeave(Event);Paint(false);
}
