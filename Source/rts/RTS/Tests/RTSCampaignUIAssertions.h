#pragma once
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Audio/RTSAudioSubsystem.h"
#include "Campaign/RTSCampaignMinimap.h"
#include "Campaign/RTSCampaignSettingsWidget.h"
#include "Campaign/RTSCampaignWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Input/HittestGrid.h"
#include "Widgets/SWindow.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace RTSCampaignUIAssertions
{
 inline void Check(FAutomationTestBase* Test,URTSCampaignWidget* HUD)
 {
  auto* Board=HUD->WidgetTree->FindWidget(TEXT("CampaignBoard"));auto* Stock=HUD->WidgetTree->FindWidget(TEXT("CampaignStockBoard"));
  Test->TestTrue(TEXT("Object panel is below both stock rows"),Board && Stock && Board->GetCachedGeometry().GetAbsolutePosition().Y>=Stock->GetCachedGeometry().GetAbsolutePosition().Y+Stock->GetCachedGeometry().GetAbsoluteSize().Y);
  auto* Scroll=Cast<UScrollBox>(HUD->WidgetTree->FindWidget(TEXT("CampaignWarScroll")));
  auto* ScrollSlot=Scroll?Cast<UVerticalBoxSlot>(Scroll->Slot):nullptr;
  Test->TestTrue(TEXT("War report has a bounded fill scroll region"),ScrollSlot && ScrollSlot->GetSize().SizeRule==ESlateSizeRule::Fill && Scroll->GetClipping()==EWidgetClipping::ClipToBounds);
  bool SaveInReport=false;
  if(auto* Page=Cast<UPanelWidget>(HUD->WidgetTree->FindWidget(TEXT("Page_Campaign"))))
  {
   TFunction<void(UWidget*)> Visit=[&](UWidget* Item){if(auto* Text=Cast<UTextBlock>(Item))SaveInReport|=Text->GetText().ToString().Contains(TEXT("保存当前战役"));if(auto* Panel=Cast<UPanelWidget>(Item))for(auto* Child:Panel->GetAllChildren())Visit(Child);};Visit(Page);
  }
  Test->TestFalse(TEXT("War report contains no save controls"),SaveInReport);
  TArray<UUserWidget*> Maps;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(HUD,Maps,URTSCampaignMinimap::StaticClass(),false);
  auto* Map=Maps.Num()?Cast<URTSCampaignMinimap>(Maps[0]):nullptr;
  Test->TestNotNull(TEXT("Interactive minimap exists"),Map);
  auto* PC=HUD->GetOwningPlayer();APawn* Pawn=PC?PC->GetPawn().Get():nullptr;
  if(Map && Pawn && FSlateApplication::IsInitialized() && !FParse::Param(FCommandLine::Get(),TEXT("NullRHI")))
  {
   const FVector Original=Pawn->GetActorLocation();const TSharedRef<SWidget> SlateMap=Map->TakeWidget();
   const bool HasSize=Map->GetCachedGeometry().GetLocalSize().X>0 && Map->GetCachedGeometry().GetLocalSize().Y>0;
   Test->TestTrue(TEXT("Minimap has live arranged geometry"),HasSize);
   if(HasSize)
   {
    const auto& Geometry=Map->GetCachedGeometry();const FVector2D Size=Geometry.GetLocalSize();
    const FVector2D A=Geometry.LocalToAbsolute(Size*FVector2D(.48,.55)),B=Geometry.LocalToAbsolute(Size*FVector2D(.54,.48));
    const TSet<FKey> Left={EKeys::LeftMouseButton},Right={EKeys::RightMouseButton},None;
    auto Event=[](FVector2D At,FVector2D Last,const TSet<FKey>& Held,FKey Button,float Wheel=0.f){return FPointerEvent(0,At,Last,Held,Button,Wheel,FModifierKeysState());};
    // Query the live painted hit grid, not a fabricated path to the widget. Never route an empty path.
    auto& App=FSlateApplication::Get();const auto Window=App.FindWidgetWindow(SlateMap);
    Test->TestTrue(TEXT("Minimap belongs to a live Slate window"),Window.IsValid());
    if(Window)
    {
     FWidgetPath HitPath(Window->GetHittestGrid().GetBubblePath(A,0.f,false,0));
     const bool HitsMap=HitPath.IsValid() && HitPath.ContainsWidget(&SlateMap.Get());
     Test->TestTrue(TEXT("Painted minimap is in the ordinary mouse hit path"),HitsMap);
     if(!HitsMap)Test->AddInfo(FString::Printf(TEXT("Minimap hit path: %s; visibility=%s"),*HitPath.ToString(),*SlateMap->GetVisibility().ToString()));
     if(HitsMap)
     {
      // A full-map edge may have no nav polygon. Zoom so this outside-widget point is still interior world terrain.
      SlateMap->OnMouseWheel(Geometry,Event(A,A,None,EKeys::Invalid,4.f));
      const FReply Down=App.RoutePointerDownEvent(HitPath,Event(A,A,Left,EKeys::LeftMouseButton));
      Test->TestTrue(TEXT("Routed left press stops at minimap rather than gameplay viewport"),Down.IsEventHandled() && Down.GetHandler()==SlateMap);
      Test->TestTrue(TEXT("Slate applies minimap mouse capture"),SlateMap->HasMouseCapture());
      const FVector PressLocation=Pawn->GetActorLocation();
      const FVector2D Outside=Geometry.LocalToAbsolute(Size*FVector2D(-.08,.48));
      FWidgetPath OutsidePath(Window->GetHittestGrid().GetBubblePath(Outside,0.f,false,0));
      const bool MoveHandled=App.RoutePointerMoveEvent(OutsidePath,Event(Outside,A,Left,EKeys::Invalid),false);
      const double DragDistance=FVector::Dist2D(PressLocation,Pawn->GetActorLocation());
      Test->AddInfo(FString::Printf(TEXT("MINIMAP_ROUTED_DRAG hit=1 handled=%d capture=%d dragging=%d distance=%.1f"),MoveHandled,SlateMap->HasMouseCapture(),Map->IsDraggingCamera(),DragDistance));
      Test->TestTrue(TEXT("Captured drag continues outside minimap"),MoveHandled && SlateMap->HasMouseCapture() && Map->IsDraggingCamera() && DragDistance>500);
      const FReply Up=App.RoutePointerUpEvent(OutsidePath,Event(Outside,Outside,None,EKeys::LeftMouseButton));
      Test->TestTrue(TEXT("Routed release is consumed outside minimap"),Up.IsEventHandled() && Up.GetHandler()==SlateMap);
      Test->TestTrue(TEXT("Slate releases capture and clears drag"),!SlateMap->HasMouseCapture() && !Map->IsDraggingCamera());
      if(SlateMap->HasMouseCapture())App.ReleaseAllPointerCapture();
      SlateMap->OnMouseButtonDoubleClick(Geometry,Event(A,A,Left,EKeys::LeftMouseButton));
     }
    }
    Test->TestTrue(TEXT("Minimap consumes left press"),SlateMap->OnMouseButtonDown(Geometry,Event(A,A,Left,EKeys::LeftMouseButton)).IsEventHandled());
    const FVector AfterPress=Pawn->GetActorLocation();SlateMap->OnMouseMove(Geometry,Event(B,A,Left,EKeys::Invalid));
    Test->TestTrue(TEXT("Held left drag moves camera after initial jump"),Map->IsDraggingCamera() && FVector::Dist2D(AfterPress,Pawn->GetActorLocation())>500);
    SlateMap->OnMouseButtonUp(Geometry,Event(B,B,None,EKeys::LeftMouseButton));
    Test->TestFalse(TEXT("Left release ends camera dragging"),Map->IsDraggingCamera());
    SlateMap->OnMouseWheel(Geometry,Event(A,A,None,EKeys::Invalid,4.f));
    Test->TestTrue(TEXT("Wheel zooms minimap"),Map->GetMapView().Zoom>1);
    const FVector2D Center=Map->GetMapView().Center;
    auto* Human=Cast<ARTSHumanController>(PC);auto* Selection=Human?Human->GetSelection():nullptr;
    const auto Selected=Selection?Selection->GetSelectedUnits():TArray<ARTSUnit*>();
    if(Selection)Selection->ClearSelection();
    SlateMap->OnMouseButtonDown(Geometry,Event(A,A,Right,EKeys::RightMouseButton));SlateMap->OnMouseMove(Geometry,Event(B,A,Right,EKeys::Invalid));SlateMap->OnMouseButtonUp(Geometry,Event(B,B,None,EKeys::RightMouseButton));
    Test->TestFalse(TEXT("Right drag pans zoomed map"),Map->GetMapView().Center.Equals(Center,1e-6));
    if(Selection)for(auto* Unit:Selected)Selection->SelectSingle(Unit,true);
    SlateMap->OnMouseButtonDoubleClick(Geometry,Event(A,A,Left,EKeys::LeftMouseButton));
    Test->TestEqual(TEXT("Double click restores full map"),Map->GetMapView().Zoom,1.0);
    SlateMap->OnMouseWheel(Geometry,Event(A,A,None,EKeys::Invalid,3.f));
    auto* FullMap=Cast<UButton>(HUD->WidgetTree->FindWidget(TEXT("ResetCampaignMinimap")));
    Test->TestNotNull(TEXT("Explicit full-map reset button exists"),FullMap);
    if(FullMap)FullMap->OnClicked.Broadcast();
    Test->TestTrue(TEXT("Full-map button restores all objective coordinates"),Map->GetMapView().Zoom==1 && Map->GetMapView().Center.Equals(FVector2D(.5,.5)));
   }
   Pawn->SetActorLocation(Original);
  }
  if(FParse::Param(FCommandLine::Get(),TEXT("NullRHI")))Test->AddInfo(TEXT("UI_PAINT_HIT_PATH_NOT_TESTED NullRHI=1 realGeometryRequired=1"));
  if(auto* Campaign=ARTSCampaign::Get(HUD))Test->TestTrue(TEXT("Prepare a private record for load confirmation checks"),Campaign->SaveCampaign());
  auto* Gear=Cast<UButton>(HUD->WidgetTree->FindWidget(TEXT("OpenCampaignSettings")));Test->TestNotNull(TEXT("Independent gear button"),Gear);
  if(Gear)Gear->OnClicked.Broadcast();
  Test->TestTrue(TEXT("Settings pauses and hides ordinary pause menu"),HUD->IsSettingsOpen() && UGameplayStatics::IsGamePaused(HUD) && !HUD->IsPauseMenuVisible());
  auto* Settings=Cast<URTSCampaignSettingsWidget>(HUD->WidgetTree->FindWidget(TEXT("CampaignSettings")));
  auto* Audio=URTSAudioSubsystem::Get(HUD);Test->TestNotNull(TEXT("Audio subsystem"),Audio);
  if(Settings && Audio)
  {
   const float Master=Audio->GetVolume(),Music=Audio->GetMusicVolume(),Effects=Audio->GetEffectsVolume();
   auto Change=[&](const TCHAR* Name,float Value){auto* Slider=Cast<USlider>(Settings->WidgetTree->FindWidget(Name));Test->TestNotNull(Name,Slider);if(Slider)Slider->OnValueChanged.Broadcast(Value);};
   Change(TEXT("MasterVolume"),.6f);Change(TEXT("MusicVolume"),.25f);Change(TEXT("EffectsVolume"),0.f);
   Test->TestEqual(TEXT("Master slider applies immediately"),Audio->GetVolume(),.6f);Test->TestEqual(TEXT("Music slider is independent"),Audio->GetMusicVolume(),.25f);Test->TestEqual(TEXT("Effects slider mutes"),Audio->GetEffectsVolume(),0.f);
   Test->TestFalse(TEXT("Muted effects reject cue"),Audio->PlayCue(ERTSAudioCue::Victory));
   auto Click=[&](const TCHAR* Name){auto* Button=Cast<UButton>(Settings->WidgetTree->FindWidget(Name));Test->TestNotNull(Name,Button);if(Button)Button->OnClicked.Broadcast();};
   Click(TEXT("LoadCampaign"));auto* Picker=Settings->WidgetTree->FindWidget(TEXT("LoadChooser"));auto* Confirm=Settings->WidgetTree->FindWidget(TEXT("LoadConfirmation"));
   Test->TestTrue(TEXT("Load opens a separate slot picker"),Picker && Picker->GetVisibility()==ESlateVisibility::Visible && Confirm && Confirm->GetVisibility()==ESlateVisibility::Collapsed);
   Click(TEXT("LoadSlot1"));Test->TestTrue(TEXT("Selecting a slot requires confirmation"),Confirm && Confirm->GetVisibility()==ESlateVisibility::Visible);Click(TEXT("CancelLoad"));Test->TestTrue(TEXT("Load confirmation cancels"),Confirm && Confirm->GetVisibility()==ESlateVisibility::Collapsed);
   Audio->SetVolume(Master);Audio->SetMusicVolume(Music);Audio->SetEffectsVolume(Effects);Settings->CloseSettings();
   Test->TestFalse(TEXT("Closing settings resumes the previously running world"),UGameplayStatics::IsGamePaused(HUD));
   UGameplayStatics::SetGamePaused(HUD,true);HUD->ShowSettings();Settings->CloseSettings();Test->TestTrue(TEXT("Opening from pause does not resume on close"),UGameplayStatics::IsGamePaused(HUD));UGameplayStatics::SetGamePaused(HUD,false);HUD->RefreshSelectedObject();
  }
 }
}
#endif
