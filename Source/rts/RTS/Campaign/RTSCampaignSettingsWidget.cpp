#include "Campaign/RTSCampaignSettingsWidget.h"
#include "Campaign/RTSCampaign.h"
#include "Camera/RTSCameraPawn.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Campaign/RTSCampaignInputShield.h"
#include "Audio/RTSAudioSubsystem.h"
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
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "UI/RTSUIStyle.h"

namespace
{
 UTextBlock* Text(UWidgetTree* Tree,const TCHAR* Caption,int32 Font=14)
 {
  auto* Label=Tree->ConstructWidget<UTextBlock>();RTSUIStyle::Text(Label,Font,FLinearColor(.87f,.92f,.95f));
  Label->SetText(FText::FromString(Caption));Label->SetAutoWrapText(false);Label->SetWrapTextAt(480);return Label;
 }
 void Percent(UTextBlock* Label,float Value)
 { if(Label)Label->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Value*100)))); }
}
int32 URTSCampaignGearIcon::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
 Layer=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
 const FVector2D Center=Geometry.GetLocalSize()*.5;const double Radius=FMath::Min(Center.X,Center.Y)*.85;
 TArray<FVector2D> Outline,Hole;
 for(int32 I=0;I<=32;++I)
 {
  const double Angle=I*2*PI/32;const double R=(I%4==1 || I%4==2)?Radius:Radius*.73;
  Outline.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*R);
  Hole.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*(Radius*.31));
 }
 FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Outline,ESlateDrawEffect::None,FLinearColor(.78f,.89f,.94f),true,1.6f);
 FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Hole,ESlateDrawEffect::None,FLinearColor(.78f,.89f,.94f),true,1.6f);
 return Layer+1;
}
UButton* URTSCampaignSettingsWidget::AddButton(UVerticalBox* Column,const TCHAR* Caption,FName Name,FName Function)
{
 auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
 auto* Label=Text(WidgetTree,Caption);Label->SetJustification(ETextJustify::Center);Button->SetContent(Label);
 FButtonStyle Style=RTSUIStyle::ButtonStyle();Style.NormalPadding=FMargin(12,10);Style.PressedPadding=FMargin(12,11,12,9);Button->SetStyle(Style);Button->SetBackgroundColor(FLinearColor::White);
 if(auto* ContentSlot=Cast<UButtonSlot>(Label->Slot))ContentSlot->SetHorizontalAlignment(HAlign_Fill);
 FScriptDelegate Delegate;Delegate.BindUFunction(this,Function);Button->OnClicked.Add(Delegate);
 Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,4));return Button;
}
USlider* URTSCampaignSettingsWidget::AddVolume(UVerticalBox* Column,const TCHAR* Caption,FName Name,FName Function,TObjectPtr<UTextBlock>& ValueLabel)
{
 auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,10,0,0));
 auto* CaptionSlot=Row->AddChildToHorizontalBox(Text(WidgetTree,Caption));CaptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 ValueLabel=Text(WidgetTree,TEXT("100%"));Row->AddChildToHorizontalBox(ValueLabel);
 auto* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),Name);Slider->SetStepSize(.01f);Slider->SetValue(1.f);
 Slider->SetSliderBarColor(FLinearColor(.12f,.2f,.24f));Slider->SetSliderHandleColor(FLinearColor(.45f,.85f,.83f));
 Column->AddChildToVerticalBox(Slider)->SetPadding(FMargin(0,8,0,6));
 FScriptDelegate Delegate;Delegate.BindUFunction(this,Function);Slider->OnValueChanged.Add(Delegate);return Slider;
}
void URTSCampaignSettingsWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();SetIsFocusable(true);
 auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
 auto* Shield=WidgetTree->ConstructWidget<URTSCampaignInputShield>();Shield->SetBrushColor(FLinearColor(0,0,0,.76f));
 auto* ShieldSlot=Canvas->AddChildToCanvas(Shield);ShieldSlot->SetAnchors(FAnchors(0,0,1,1));ShieldSlot->SetOffsets(FMargin(0));
 auto* Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("SettingsPanel"));Panel->SetBrushColor(FLinearColor(.035f,.065f,.085f,1));Panel->SetPadding(FMargin(24));
 auto* Cell=Canvas->AddChildToCanvas(Panel);Cell->SetAnchors(FAnchors(.5f,.5f));Cell->SetAlignment(FVector2D(.5,.5));Cell->SetAutoSize(true);
 PanelSize=WidgetTree->ConstructWidget<USizeBox>();PanelSize->SetWidthOverride(560);PanelSize->SetHeightOverride(650);Panel->SetContent(PanelSize);
 auto* Pages=WidgetTree->ConstructWidget<UVerticalBox>();PanelSize->SetContent(Pages);
 SettingsPage=WidgetTree->ConstructWidget<UVerticalBox>();Pages->AddChildToVerticalBox(SettingsPage)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 auto* Column=SettingsPage.Get();
 Column->AddChildToVerticalBox(Text(WidgetTree,TEXT("设置与战役记录"),24));
 Column->AddChildToVerticalBox(Text(WidgetTree,TEXT("打开设置会暂停战场；音量实时生效，关闭时记住。"),12))->SetPadding(FMargin(0,8,0,12));
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("SettingsScroll"));Scroll->SetClipping(EWidgetClipping::ClipToBounds);
 Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Body);
 Body->AddChildToVerticalBox(Text(WidgetTree,TEXT("声音"),17));
 MasterSlider=AddVolume(Body,TEXT("总音量"),TEXT("MasterVolume"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,MasterChanged),MasterValue);
 MusicSlider=AddVolume(Body,TEXT("背景音乐"),TEXT("MusicVolume"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,MusicChanged),MusicValue);
 EffectsSlider=AddVolume(Body,TEXT("音效"),TEXT("EffectsVolume"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,EffectsChanged),EffectsValue);
 Body->AddChildToVerticalBox(Text(WidgetTree,TEXT("画面与镜头"),17))->SetPadding(FMargin(0,20,0,8));
 CameraSlider=AddVolume(Body,TEXT("相机移动速度"),TEXT("CameraSpeed"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CameraSpeedChanged),CameraValue);
 DisplayStatus=Text(WidgetTree,TEXT(""),12);Body->AddChildToVerticalBox(DisplayStatus)->SetPadding(FMargin(0,4,0,4));
 AddButton(Body,TEXT("切换分辨率"),TEXT("CycleResolution"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CycleResolution));
 AddButton(Body,TEXT("切换窗口模式"),TEXT("CycleWindowMode"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CycleWindowMode));
 AddButton(Body,TEXT("切换垂直同步"),TEXT("ToggleVSync"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,ToggleVSync));
 Body->AddChildToVerticalBox(Text(WidgetTree,TEXT("存档"),17))->SetPadding(FMargin(0,20,0,8));
 AddButton(Body,TEXT("保存……"),TEXT("SaveCampaign"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,SaveCampaign));
 SavePage=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("SavePage"));Pages->AddChildToVerticalBox(SavePage)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 SavePage->AddChildToVerticalBox(Text(WidgetTree,TEXT("选择保存位置"),24))->SetPadding(FMargin(0,0,0,24));
 SaveChooser=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("SaveChooser"));SavePage->AddChildToVerticalBox(SaveChooser);
 SaveSlotButtons.Add(AddButton(SaveChooser,TEXT("存档位置 1"),TEXT("SaveToSlot1"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,SaveToSlot1)));
 SaveSlotButtons.Add(AddButton(SaveChooser,TEXT("存档位置 2"),TEXT("SaveToSlot2"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,SaveToSlot2)));
 SaveSlotButtons.Add(AddButton(SaveChooser,TEXT("存档位置 3"),TEXT("SaveToSlot3"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,SaveToSlot3)));
 SaveConfirmation=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("SaveConfirmation"));SavePage->AddChildToVerticalBox(SaveConfirmation);
 SavePrompt=Text(WidgetTree,TEXT(""),17);SaveConfirmation->AddChildToVerticalBox(SavePrompt)->SetPadding(FMargin(0,12));
 ConfirmSaveButton=AddButton(SaveConfirmation,TEXT("确认保存"),TEXT("ConfirmSave"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,ConfirmSave));
 AddButton(SaveConfirmation,TEXT("取消"),TEXT("CancelSaveConfirmation"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CancelSaveConfirmation));
 SaveConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 SaveStatus=Text(WidgetTree,TEXT(""),13);SaveStatus->SetColorAndOpacity(FLinearColor(1,.8f,.48f));SavePage->AddChildToVerticalBox(SaveStatus)->SetPadding(FMargin(0,12));
 AddButton(SavePage,TEXT("返回  [Esc]"),TEXT("CancelSaveSlot"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CancelSaveSlot));
 SavePage->SetVisibility(ESlateVisibility::Collapsed);
 AddButton(Body,TEXT("读取……"),TEXT("LoadCampaign"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,RequestLoad));
 LoadChooser=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("LoadChooser"));Body->AddChildToVerticalBox(LoadChooser);
 LoadChooser->AddChildToVerticalBox(Text(WidgetTree,TEXT("读取哪一个存档？"),13))->SetPadding(FMargin(0,6));
 AddButton(LoadChooser,TEXT("槽 1 · 主档"),TEXT("LoadSlot1"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,RequestLoadSlot1));
 AddButton(LoadChooser,TEXT("槽 2"),TEXT("LoadSlot2"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,RequestLoadSlot2));
 AddButton(LoadChooser,TEXT("槽 3"),TEXT("LoadSlot3"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,RequestLoadSlot3));
 AddButton(LoadChooser,TEXT("取消读取"),TEXT("CancelLoadSlot"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CancelLoad));
 LoadChooser->SetVisibility(ESlateVisibility::Collapsed);
 LoadConfirmation=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("LoadConfirmation"));Body->AddChildToVerticalBox(LoadConfirmation);
 LoadConfirmation->AddChildToVerticalBox(Text(WidgetTree,TEXT("读取会丢失未保存进度。确认读取所选存档？"),13))->SetPadding(FMargin(0,8));
 AddButton(LoadConfirmation,TEXT("确认读取"),TEXT("ConfirmLoad"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,ConfirmLoad));
 AddButton(LoadConfirmation,TEXT("取消读取"),TEXT("CancelLoad"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CancelLoad));
 LoadConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 AddButton(Body,TEXT("重置今天（回到本日开局）"),TEXT("ResetToday"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,ResetToday));
 ResetConfirmation=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("ResetConfirmation"));Body->AddChildToVerticalBox(ResetConfirmation);
 ResetConfirmation->AddChildToVerticalBox(Text(WidgetTree,TEXT("回到本日开局？本日新采的资源、新造的兵、新完成的施工都会撤销。"),13))->SetPadding(FMargin(0,8));
 AddButton(ResetConfirmation,TEXT("确认重置今天"),TEXT("ConfirmResetToday"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,ConfirmResetToday));
 AddButton(ResetConfirmation,TEXT("取消重置"),TEXT("CancelResetToday"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CancelResetToday));
 ResetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 Body->AddChildToVerticalBox(Text(WidgetTree,TEXT("保存会写入所选槽，并把它设为当前存档：之后“继续战役”和自动档都跟着它。重置今天需要脱战，回到本日开头那一刻（换日时自动记录）。"),12))->SetPadding(FMargin(0,10));
 Status=Text(WidgetTree,TEXT(""),13);Status->SetColorAndOpacity(FLinearColor(1,.8f,.48f));Body->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,4,0,10));
 AddButton(Body,TEXT("FocusCampaignHome"),TEXT("镜头返回主城"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,FocusHome));
 Body->AddChildToVerticalBox(Text(WidgetTree,TEXT("阵营识别配色"),17))->SetPadding(FMargin(0,20,0,8));
 AddButton(Body,TEXT("切换暮棘盟配色"),TEXT("CyclePurple"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CyclePurple));
 AddButton(Body,TEXT("切换赤金沙团配色"),TEXT("CycleYellow"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CycleYellow));
 AddButton(Body,TEXT("切换赤焰卫配色"),TEXT("CycleRed"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CycleRed));
 AddButton(Body,TEXT("切换蔷薇庭配色"),TEXT("CyclePink"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CyclePink));
 AddButton(Column,TEXT("关闭设置  [Esc]"),TEXT("CloseSettings"),GET_FUNCTION_NAME_CHECKED(URTSCampaignSettingsWidget,CloseSettings));
 SetVisibility(ESlateVisibility::Collapsed);
}
void URTSCampaignSettingsWidget::OpenSettings()
{
 if(IsOpen())return;
 bResumeOnClose=!UGameplayStatics::IsGamePaused(this);
 if(bResumeOnClose)UGameplayStatics::SetGamePaused(this,true);
 // Always reopen on the three entry points; no half-finished chooser survives a close.
 PendingSlot=-1;
 PendingSaveSlot=-1;SettingsPage->SetVisibility(ESlateVisibility::Visible);SavePage->SetVisibility(ESlateVisibility::Collapsed);
 SaveConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(SaveChooser)SaveChooser->SetVisibility(ESlateVisibility::Collapsed);
 if(LoadChooser)LoadChooser->SetVisibility(ESlateVisibility::Collapsed);
 if(LoadConfirmation)LoadConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(ResetConfirmation)ResetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 SyncAudio();
 if(CameraSlider){CameraSlider->SetMinValue(.5f);CameraSlider->SetMaxValue(2.f);CameraSlider->SetValue(ARTSCameraPawn::GetPanSpeedScale());}
 if(CameraValue)CameraValue->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(ARTSCameraPawn::GetPanSpeedScale()*100))));
 RefreshDisplayStatus();RefreshStatus();SetVisibility(ESlateVisibility::Visible);SetKeyboardFocus();
 if(auto* PC=GetOwningPlayer()){FInputModeUIOnly Mode;Mode.SetWidgetToFocus(TakeWidget());Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);}
}
void URTSCampaignSettingsWidget::CloseSettings()
{
 if(!IsOpen())return;
 if(auto* Audio=URTSAudioSubsystem::Get(this))Audio->SaveVolumeSettings();
 SetVisibility(ESlateVisibility::Collapsed);if(bResumeOnClose)UGameplayStatics::SetGamePaused(this,false);bResumeOnClose=false;
 if(auto* PC=GetOwningPlayer()){FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);}
}
void URTSCampaignSettingsWidget::NativeTick(const FGeometry& Geometry,float DeltaTime)
{
 Super::NativeTick(Geometry,DeltaTime);if(!IsOpen())return;
 if(Geometry.GetLocalSize().Y>0){PanelSize->SetHeightOverride(FMath::Clamp(float(Geometry.GetLocalSize().Y)-96.f,280.f,650.f));PanelSize->SetWidthOverride(FMath::Clamp(float(Geometry.GetLocalSize().X)-96.f,280.f,560.f));}
}
FReply URTSCampaignSettingsWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
 if(Event.GetKey()==EKeys::Escape)
 {
  if(SavePage && SavePage->GetVisibility()==ESlateVisibility::Visible)
  {
   if(PendingSaveSlot>=0)CancelSaveConfirmation();else CancelSaveSlot();
  }
  else CloseSettings();
  return FReply::Handled();
 }
 if(Event.GetKey()==EKeys::P){CloseSettings();return FReply::Handled();}
 return FReply::Handled(); // No game hotkeys while a modal is focused.
}
void URTSCampaignSettingsWidget::RefreshStatus()
{ if(auto* Campaign=ARTSCampaign::Get(this))Status->SetText(FText::FromString(Campaign->GetMessage())); }
void URTSCampaignSettingsWidget::SyncAudio()
{
 if(auto* Audio=URTSAudioSubsystem::Get(this))
 {
  MasterSlider->SetValue(Audio->GetVolume());MusicSlider->SetValue(Audio->GetMusicVolume());EffectsSlider->SetValue(Audio->GetEffectsVolume());
  Percent(MasterValue,Audio->GetVolume());Percent(MusicValue,Audio->GetMusicVolume());Percent(EffectsValue,Audio->GetEffectsVolume());
 }
}
void URTSCampaignSettingsWidget::MasterChanged(float Value){if(auto* Audio=URTSAudioSubsystem::Get(this))Audio->SetVolume(Value);SyncAudio();}
void URTSCampaignSettingsWidget::MusicChanged(float Value){if(auto* Audio=URTSAudioSubsystem::Get(this))Audio->SetMusicVolume(Value);SyncAudio();}
void URTSCampaignSettingsWidget::EffectsChanged(float Value){if(auto* Audio=URTSAudioSubsystem::Get(this))Audio->SetEffectsVolume(Value);SyncAudio();}
void URTSCampaignSettingsWidget::SaveCampaign()
{
 PendingSaveSlot=-1;
 SettingsPage->SetVisibility(ESlateVisibility::Collapsed);
 SavePage->SetVisibility(ESlateVisibility::Visible);
 SaveChooser->SetVisibility(ESlateVisibility::Visible);
 SaveConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 SaveStatus->SetText(FText::GetEmpty());
 RefreshSaveSlots();
 SetKeyboardFocus();
}
void URTSCampaignSettingsWidget::RefreshSaveSlots()
{
 for(int32 Index=0;Index<SaveSlotButtons.Num();++Index)
 {
  ARTSCampaign::FRTSSlotInfo Info;ARTSCampaign::DescribeSlot(Index,Info);
  const FString Caption=Info.bExists?FString::Printf(TEXT("存档位置 %d · 已有存档 · %s"),Index+1,*Info.Modified.ToString(TEXT("%Y-%m-%d %H:%M"))):FString::Printf(TEXT("存档位置 %d · 空"),Index+1);
  if(auto* Label=Cast<UTextBlock>(SaveSlotButtons[Index]->GetContent()))Label->SetText(FText::FromString(Caption));
 }
}
void URTSCampaignSettingsWidget::RequestSaveSlot(int32 Index)
{
 if(!SavePage || SavePage->GetVisibility()!=ESlateVisibility::Visible || Index<0 || Index>2)return;
 PendingSaveSlot=Index;bConfirmingOverwrite=ARTSCampaign::HasSlot(Index);
 SaveChooser->SetVisibility(ESlateVisibility::Collapsed);
 SaveConfirmation->SetVisibility(ESlateVisibility::Visible);
 const FString Prompt=bConfirmingOverwrite?FString::Printf(TEXT("存档位置 %d 已有存档，是否覆盖？"),Index+1):FString::Printf(TEXT("是否保存到存档位置 %d？"),Index+1);
 SavePrompt->SetText(FText::FromString(Prompt));
 if(auto* Label=Cast<UTextBlock>(ConfirmSaveButton->GetContent()))Label->SetText(FText::FromString(bConfirmingOverwrite?TEXT("确认覆盖"):TEXT("确认保存")));
 SaveStatus->SetText(FText::GetEmpty());SetKeyboardFocus();
}
void URTSCampaignSettingsWidget::ConfirmSave()
{
 if(PendingSaveSlot<0 || SavePage->GetVisibility()!=ESlateVisibility::Visible || SaveConfirmation->GetVisibility()!=ESlateVisibility::Visible)return;
 // Recheck existence: a new record appearing after selection needs explicit overwrite consent.
 if(!bConfirmingOverwrite && ARTSCampaign::HasSlot(PendingSaveSlot)){RequestSaveSlot(PendingSaveSlot);return;}
 CommitSaveSlot(PendingSaveSlot);
}
void URTSCampaignSettingsWidget::CommitSaveSlot(int32 Index)
{
 auto* C=ARTSCampaign::Get(this);
 if(!C){SaveStatus->SetText(FText::FromString(TEXT("当前没有可保存的战役。")));return;}
 if(!C->SaveToSlot(Index)){SaveStatus->SetText(FText::FromString(FString(TEXT("未保存："))+C->GetMessage()));return;}
 ARTSCampaign::SetActiveSlot(Index);
 PendingSaveSlot=-1;
 CloseSettings();
}
void URTSCampaignSettingsWidget::SaveToSlot1(){RequestSaveSlot(0);}
void URTSCampaignSettingsWidget::SaveToSlot2(){RequestSaveSlot(1);}
void URTSCampaignSettingsWidget::SaveToSlot3(){RequestSaveSlot(2);}
void URTSCampaignSettingsWidget::CancelSaveConfirmation()
{
 PendingSaveSlot=-1;
 SaveConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 SaveChooser->SetVisibility(ESlateVisibility::Visible);
 SaveStatus->SetText(FText::GetEmpty());RefreshSaveSlots();SetKeyboardFocus();
}
void URTSCampaignSettingsWidget::CancelSaveSlot()
{
 PendingSaveSlot=-1;
 SavePage->SetVisibility(ESlateVisibility::Collapsed);
 SettingsPage->SetVisibility(ESlateVisibility::Visible);
 SetKeyboardFocus();
}
void URTSCampaignSettingsWidget::RequestLoad()
{
 PendingSlot=-1;
 if(LoadChooser)LoadChooser->SetVisibility(ESlateVisibility::Visible);
 if(SaveChooser)SaveChooser->SetVisibility(ESlateVisibility::Collapsed);
 if(LoadConfirmation)LoadConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(ResetConfirmation)ResetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(Status)Status->SetText(FText::FromString(TEXT("选择要读取的存档；读取会丢失当前未保存进度。")));
}
void URTSCampaignSettingsWidget::RequestLoadSlot1(){RequestLoadSlot(0);}
void URTSCampaignSettingsWidget::RequestLoadSlot2(){RequestLoadSlot(1);}
void URTSCampaignSettingsWidget::RequestLoadSlot3(){RequestLoadSlot(2);}
void URTSCampaignSettingsWidget::RequestLoadSlot(int32 Index)
{
 if(!ARTSCampaign::HasSlot(Index))
 {
  if(Status)Status->SetText(FText::FromString(FString::Printf(TEXT("槽 %d 还没有记录，请先保存或用开始界面读取。"),Index+1)));
  return;
 }
 PendingSlot=Index;
 if(LoadChooser)LoadChooser->SetVisibility(ESlateVisibility::Collapsed);
 if(LoadConfirmation)LoadConfirmation->SetVisibility(ESlateVisibility::Visible);
 if(Status)Status->SetText(FText::FromString(FString::Printf(TEXT("将读取槽 %d，确认后立即生效。"),Index+1)));
}
void URTSCampaignSettingsWidget::ConfirmLoad()
{
 if(!LoadConfirmation || LoadConfirmation->GetVisibility()!=ESlateVisibility::Visible || PendingSlot<0)return;
 auto* C=ARTSCampaign::Get(this);const int32 Index=PendingSlot;PendingSlot=-1;
 if(!C){ if(Status)Status->SetText(FText::FromString(TEXT("当前没有可读取的战役。"))); return; }
 const bool bLoaded=C->LoadFromSlot(Index);
 if(LoadConfirmation)LoadConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(bLoaded){ ARTSCampaign::SetActiveSlot(Index);CloseSettings();return; }
 if(Status)Status->SetText(FText::FromString(FString(TEXT("读取失败："))+C->GetMessage()));
 RefreshStatus();
}
void URTSCampaignSettingsWidget::CancelLoad()
{
 PendingSlot=-1;
 if(LoadConfirmation)LoadConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(LoadChooser)LoadChooser->SetVisibility(ESlateVisibility::Collapsed);
 if(Status)Status->SetText(FText::FromString(TEXT("已取消读取，当前进度保持不变。")));
}
void URTSCampaignSettingsWidget::ResetToday()
{
 auto* C=ARTSCampaign::Get(this);
 if(!C) { if(Status)Status->SetText(FText::FromString(TEXT("当前没有可重置的战役。"))); return; }
 if(!C->HasDaySnapshot())
 {
  if(Status)Status->SetText(FText::FromString(TEXT("本日还没有开局快照：换日起且脱战后会自动记录。")));
  return;
 }
 if(ResetConfirmation)ResetConfirmation->SetVisibility(ESlateVisibility::Visible);
 if(Status)Status->SetText(FText::FromString(FString::Printf(TEXT("确认后回到本日（第 %d 天）开局那一刻；需要脱战。"),C->GetDay()+1)));
}
void URTSCampaignSettingsWidget::ConfirmResetToday()
{
 auto* C=ARTSCampaign::Get(this);
 if(!C)return;
 const bool bReset=C->LoadDaySnapshot();
 if(ResetConfirmation)ResetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(bReset){CloseSettings();return;}
 if(Status)Status->SetText(FText::FromString(FString(TEXT("无法重置："))+C->GetMessage()));
 RefreshStatus();
}
void URTSCampaignSettingsWidget::CancelResetToday()
{
 if(ResetConfirmation)ResetConfirmation->SetVisibility(ESlateVisibility::Collapsed);
 if(Status)Status->SetText(FText::FromString(TEXT("已取消重置，当前进度保持不变。")));
}
void URTSCampaignSettingsWidget::FocusHome(){if(auto* C=ARTSCampaign::Get(this))C->FocusHome();RefreshStatus();}
void URTSCampaignSettingsWidget::CyclePurple(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(0);RefreshStatus();}
void URTSCampaignSettingsWidget::CycleYellow(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(1);RefreshStatus();}
void URTSCampaignSettingsWidget::CycleRed(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(2);RefreshStatus();}
void URTSCampaignSettingsWidget::CyclePink(){if(auto* C=ARTSCampaign::Get(this))C->CycleFactionColor(3);RefreshStatus();}

void URTSCampaignSettingsWidget::CameraSpeedChanged(float Value)
{
 ARTSCameraPawn::SetPanSpeedScale(FMath::Clamp(Value,.5f,2.f));
 if(CameraValue)CameraValue->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(ARTSCameraPawn::GetPanSpeedScale()*100))));
}
void URTSCampaignSettingsWidget::RefreshDisplayStatus()
{
 if(!DisplayStatus)return;
 UGameUserSettings* S=GEngine?GEngine->GameUserSettings:nullptr;
 if(!S){DisplayStatus->SetText(FText::FromString(TEXT("当前环境没有可调整的显示设置。")));return;}
 const FIntPoint Res=S->GetScreenResolution();
 const TCHAR* Mode=S->GetFullscreenMode()==EWindowMode::Fullscreen?TEXT("全屏"):S->GetFullscreenMode()==EWindowMode::WindowedFullscreen?TEXT("无边框全屏"):TEXT("窗口");
 DisplayStatus->SetText(FText::FromString(FString::Printf(TEXT("%dx%d · %s · 垂直同步%s"),Res.X,Res.Y,Mode,S->IsVSyncEnabled()?TEXT("开"):TEXT("关"))));
}
void URTSCampaignSettingsWidget::CycleResolution()
{
 UGameUserSettings* S=GEngine?GEngine->GameUserSettings:nullptr;if(!S)return;
 static const FIntPoint Modes[]={FIntPoint(1280,720),FIntPoint(1600,900),FIntPoint(1920,1080),FIntPoint(2560,1440)};
 const FIntPoint Current=S->GetScreenResolution();int32 Next=0;
 for(int32 I=0;I<UE_ARRAY_COUNT(Modes);++I)if(Modes[I]==Current)Next=(I+1)%UE_ARRAY_COUNT(Modes);
 S->SetScreenResolution(Modes[Next]);S->ApplySettings(false);S->SaveSettings();RefreshDisplayStatus();
}
void URTSCampaignSettingsWidget::CycleWindowMode()
{
 UGameUserSettings* S=GEngine?GEngine->GameUserSettings:nullptr;if(!S)return;
 const EWindowMode::Type Current=S->GetFullscreenMode();
 const EWindowMode::Type Next=Current==EWindowMode::Windowed?EWindowMode::Fullscreen
  :Current==EWindowMode::Fullscreen?EWindowMode::WindowedFullscreen:EWindowMode::Windowed;
 S->SetFullscreenMode(Next);S->ApplySettings(false);S->SaveSettings();RefreshDisplayStatus();
}
void URTSCampaignSettingsWidget::ToggleVSync()
{
 UGameUserSettings* S=GEngine?GEngine->GameUserSettings:nullptr;if(!S)return;
 S->SetVSyncEnabled(!S->IsVSyncEnabled());S->ApplySettings(false);S->SaveSettings();RefreshDisplayStatus();
}
