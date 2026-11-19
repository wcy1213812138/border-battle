#include "Tests/RTSCampaignPackageSmoke.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignEntry.h"
#include "Campaign/RTSCampaignWidget.h"
#include "Campaign/RTSCampaignSettingsWidget.h"
#include "Campaign/RTSCampaignStorage.h"
#include "Economy/RTSGathererComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Audio/RTSAudioSubsystem.h"
#include "Components/Slider.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSInfantryCatalog.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSProducerComponent.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Units/RTSUnit.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Components/Border.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Misc/Guid.h"
#include "Framework/Application/SlateApplication.h"
#include "AIController.h"
#include "NavigationSystem.h"

void URTSCampaignPackageSmoke::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING && !WITH_EDITOR
 bEnabled=FParse::Value(FCommandLine::Get(),TEXT("RTSCampaignSmoke="),Mode);
 if(bEnabled)
 {
  FString Slot,UserRoot;
  FParse::Value(FCommandLine::Get(),TEXT("RTSSmokeSlot="),Slot);FParse::Value(FCommandLine::Get(),TEXT("RTSSmokeUserRoot="),UserRoot);
  const FString Prefix=TEXT("Automation_Smoke_");FGuid Id;
  if(!Slot.StartsWith(Prefix) || Slot.Len()!=Prefix.Len()+32 || !FGuid::ParseExact(Slot.Mid(Prefix.Len()),EGuidFormats::Digits,Id) ||
     UserRoot.IsEmpty() || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),FPaths::ConvertRelativePathToFull(UserRoot)) ||
     !FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")))
  {Finish(false,TEXT("Explicit GUID slot, verified isolated Saved directory and offscreen mode required"));return;}
  ARTSCampaign::SetSaveSlotOverride(*Slot);
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE_ISOLATED saved=%s slot=%s"),*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),*Slot);
  Began=Phase=FPlatformTime::Seconds();
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE begin mode=%s slot=%s"),*Mode,*ARTSCampaign::SaveSlotName());
 }
#endif
}
void URTSCampaignPackageSmoke::Deinitialize()
{
 if(!Mode.IsEmpty())ARTSCampaign::SetSaveSlotOverride(nullptr);
 Super::Deinitialize();
}
void URTSCampaignPackageSmoke::Finish(bool bSuccess,const FString& Reason)
{
 bEnabled=false;
 if(bSuccess){UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE PASS mode=%s %s"),*Mode,*Reason);}
 else {UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_SMOKE FAIL mode=%s stage=%d %s"),*Mode,Stage,*Reason);}
 FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
}
bool URTSCampaignPackageSmoke::EnsureBuilt(ARTSCampaign* Campaign,FName Kind)
{
 if(auto* B=Campaign->FindServiceBuilding(Kind))return B->IsComplete();
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->GetTeam()==0 && It->IsAlive() && It->GetBuildingId()==Kind)return false;
 auto* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());if(!PC || !Worker.IsValid())return false;
 PC->SelectBuilding(nullptr);PC->GetSelection()->SelectSingle(Worker.Get(),false);
 for(int32 R=0;R<3;++R)for(int32 I=0;I<8;++I)
 {
  const double A=I*UE_PI/4,Radius=900+R*400;
  PC->BeginBuildingPlacement(Kind);
  if(PC->ConfirmBuildingPlacement(RTSCampaignTerrain::Ground(Campaign->HomeLocation()+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0))))return false;
 }
 PC->CancelBuildingPlacement();return false;
}
void URTSCampaignPackageSmoke::Tick(float DeltaTime)
{
#if !UE_BUILD_SHIPPING && !WITH_EDITOR
 if(!bEnabled)return;
 if(!FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsRenderingOffScreen()){Finish(false,TEXT("Offscreen Slate required"));return;}
 for(const auto& Window:FSlateApplication::Get().GetTopLevelWindows())if(Window->GetNativeWindow() && Window->GetNativeWindow()->GetOSWindowHandle()){Finish(false,TEXT("Native window detected"));return;}
 const double Now=FPlatformTime::Seconds();
 if(Now-Began>480){Finish(false,TEXT("480 second watchdog"));return;}
 UWorld* World=GetWorld();if(!World || !World->HasBegunPlay())return;
 auto* Campaign=ARTSCampaign::Get(this);
 auto FindEntry=[&]()->URTSCampaignEntryWidget*
 {
  TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,URTSCampaignEntryWidget::StaticClass(),true);
  for(auto* W:Widgets)if(W->IsInViewport())return Cast<URTSCampaignEntryWidget>(W);
  return nullptr;
 };
 auto Click=[](UUserWidget* Widget,const TCHAR* Name)->bool
 {
  auto* Button=Widget && Widget->WidgetTree?Cast<UButton>(Widget->WidgetTree->FindWidget(FName(Name))):nullptr;
  if(!Button || !Button->GetIsEnabled())return false;
  Button->OnClicked.Broadcast();return true;
 };
 if(auto* Entry=FindEntry();Entry && Entry->HasEntryFailed())
 {
  if(Mode==TEXT("Recovery") && Stage==93)return; // waiting for confirmed level travel
  if(Mode!=TEXT("Recovery") || Stage!=90){Finish(false,TEXT("Campaign entry reported a failure"));return;}
  // Backup validation is internal smoke coverage, not a player-facing recovery button.
  UGameplayStatics::SetGamePaused(this,false);
  UGameplayStatics::OpenLevel(this,FName(RTSCampaignEntry::CampaignMap()),true,TEXT("CampaignContinue?CampaignPrevious"));
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE corrupt_primary_rejected internal_backup_check=1"));Stage=93;return;
 }
 if(Stage==0)
 {
  auto* Entry=FindEntry();if(!Entry || Now-Began<3)return;
  if(Campaign){Finish(false,TEXT("Frontend unexpectedly spawned a campaign"));return;}
  if(!IFileManager::Get().FileExists(*(FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf")))){Finish(false,TEXT("Chinese font not staged"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/StandaloneMenu.png"),true,false);
  Stage=1;Phase=Now;return;
 }
 if(Stage==1)
 {
  if(Now-Phase<1)return;
  auto* Entry=FindEntry();if(!Entry)return;
  if(Mode==TEXT("MenuOnly")){Finish(true,TEXT("Frontend, font, screenshot"));return;}
  if(Mode==TEXT("Continue") || Mode==TEXT("Recovery"))
  {
   if(!Click(Entry,TEXT("ContinueCampaign"))){Finish(false,TEXT("Continue button disabled"));return;}
   Stage=90;return;
  }
  if(!Click(Entry,TEXT("NewCampaign")) || !Entry->IsConfirmingNewCampaign()){Finish(false,TEXT("New-campaign confirmation missing"));return;}
  if(!Click(Entry,TEXT("CancelNewCampaign")) || Entry->IsConfirmingNewCampaign()){Finish(false,TEXT("New-campaign cancellation failed"));return;}
  if(!Click(Entry,TEXT("NewCampaign")) || !Click(Entry,TEXT("ConfirmNewCampaign"))){Finish(false,TEXT("New-campaign action failed"));return;}
  Stage=2;return;
 }
 if(Stage==90 || Stage==93)
 {
  if(!Campaign || !Campaign->IsReady() || FindEntry())return;
  TArray<UUserWidget*> HUDs;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,HUDs,URTSCampaignWidget::StaticClass(),true);
  auto* Guide=HUDs.Num()?HUDs[0]->WidgetTree->FindWidget(TEXT("AdvisorDialogue")):nullptr;
  if(!Guide || Guide->GetVisibility()!=ESlateVisibility::Collapsed){Finish(false,TEXT("Continue left a stale tutorial overlay"));return;}
  if(Campaign->GetGuideStep()!=7 || Campaign->GetLevel(TEXT("Swordman"))!=2){Finish(false,TEXT("Continued progress mismatch"));return;}
  auto* RestartSnapshot=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(Mode==TEXT("Recovery")?RTSCampaignStorage::PreviousSlot(ARTSCampaign::SaveSlotName()):ARTSCampaign::SaveSlotName()));
  int32 SavedBarracks=0,LiveBarracks=0,SavedResearch=0;
  if(RestartSnapshot)for(int32 I=0;I<RestartSnapshot->BuildingKinds.Num();++I)if(RestartSnapshot->BuildingTeams[I]==0){SavedBarracks+=RestartSnapshot->BuildingKinds[I]==TEXT("Barracks");SavedResearch+=RestartSnapshot->BuildingKinds[I]==TEXT("ResearchInstitute");}
  for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetBuildingId()==TEXT("Barracks"))++LiveBarracks;
  if(!RestartSnapshot || SavedBarracks<3 || LiveBarracks!=SavedBarracks || SavedResearch!=1){Finish(false,TEXT("Paid guide/frontline/busy buildings missing after restart"));return;}
  auto* Audio=URTSAudioSubsystem::Get(this);
  if(!Audio || !FMath::IsNearlyEqual(Audio->GetVolume(),.6f) || !FMath::IsNearlyEqual(Audio->GetMusicVolume(),.25f) || !FMath::IsNearlyEqual(Audio->GetEffectsVolume(),.4f)){Finish(false,TEXT("Audio settings did not persist across process restart"));return;}
  auto* Snapshot=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(Mode==TEXT("Recovery")?RTSCampaignStorage::PreviousSlot(ARTSCampaign::SaveSlotName()):ARTSCampaign::SaveSlotName()));if(!Snapshot || Snapshot->Version!=URTSCampaignSave::SnapshotVersion){Finish(false,TEXT("New economic snapshot missing after restart"));return;}
  int32 SavedCargo=0,LiveCargo=0,Queue=0,Swords=0,SavedSwords=0;bool Construction=false;
  for(const auto& U:Snapshot->Units)if(U.Team==0 && U.Kind==TEXT("Peasant"))SavedCargo+=U.Economy.Carried;
  for(const auto& B:Snapshot->BuildingWork)for(const auto& Paid:B.Production.Orders)SavedSwords+=Paid.Kind==TEXT("Swordman");
  const int32 SnapshotWood=Snapshot->Stock.FindRef(ERTSResource::Wood);
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->IsAlive() && It->GetTeam()==0){LiveCargo+=It->GetUnitData()->bWorker?It->GetGatherer()->GetCarriedAmount():0;Swords+=It->GetUnitData()->BalanceId==TEXT("Swordman");}
  for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->IsAlive() && It->GetTeam()==0){Queue+=It->GetProducer()->GetQueue().Num();Construction|=!It->IsComplete();}
  // The world resumes after load: a full-cargo worker may legitimately deliver, and a
  // near-complete paid order may legitimately finish. Assert conservation, not frozen time.
  const URTSResourceComponent* LiveStock=URTSBalanceSubsystem::Get(this)->GetResources(0);
  const int32 Deposited=LiveStock?FMath::Max(0,LiveStock->GetAmount(ERTSResource::Wood)-SnapshotWood):0;
  const bool bCargoConserved=(LiveCargo==SavedCargo) || (LiveCargo==0 && Deposited>=SavedCargo);
  const bool bPaidConserved=(Queue==SavedSwords) || (Queue>=0 && Queue+Swords>=SavedSwords);
  if(SavedCargo<=0 || !bCargoConserved || !bPaidConserved || !Construction){Finish(false,FString::Printf(TEXT("Cross-process cargo/paid FIFO mismatch: savedCargo=%d liveCargo=%d deposited=%d savedOrders=%d liveQueue=%d liveSwords=%d construction=%d"),SavedCargo,LiveCargo,Deposited,SavedSwords,Queue,Swords,Construction));return;}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE busy_continue cargo=%d/%d orders=%d queue=%d producedSwords=%d construction=1"),LiveCargo,SavedCargo,SavedSwords,Queue,Swords);
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/StandaloneContinued.png"),true,false);
  Stage=91;Phase=Now;return;
 }
 if(Stage==91){if(Now-Phase>1){RTSCampaignEntry::OpenMenu(this);Stage=92;}return;}
 if(Stage==92){if(FindEntry() && !Campaign)Finish(true,TEXT("Restarted process -> continue -> campaign -> menu"));return;}
 if(Stage==11){if(FindEntry() && !Campaign)Finish(true,TEXT("Menu -> confirmation/cancel -> new -> gather -> upgrade -> train -> combat -> save/load -> pause -> menu"));return;}
 if(!Campaign || !Campaign->IsReady() || FindEntry())return;
 auto* Balance=URTSBalanceSubsystem::Get(this);auto* Stock=Balance?Balance->GetResources(0):nullptr;if(!Stock)return;
 if(Stage==2)
 {
  for(const auto& Entry:RTSInfantryCatalog::Entries())
   if(!Balance->CreateUnitData(Entry.Id,this)){Finish(false,FString(TEXT("Missing cooked unit: "))+Entry.Id);return;}
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->GetTeam()==0 && It->GetUnitData()->bWorker){Worker=*It;break;}
  if(!Worker.IsValid()){Finish(false,TEXT("No initial worker"));return;}
  Campaign->AdvanceGuide();Worker->SetSelected(true);Stage=3;return;
 }
 if(Stage==3)
 {
  if(Campaign->GetGuideStep()<2)return;
  WorkerStart=Worker->GetActorLocation();FRTSOrder Order;Order.Type=ERTSOrderType::Move;Order.TargetLocation=WorkerStart+FVector(0,350,-82);Worker->ReceiveOrder(Order);Stage=4;return;
 }
 if(Stage==4)
 {
  if(Campaign->GetGuideStep()<3 || FVector::Dist2D(Worker->GetActorLocation(),WorkerStart)<210)return;
  ARTSResourceNode* Wood=nullptr;for(TActorIterator<ARTSResourceNode> It(World);It;++It)if(It->GetResourceType()==ERTSResource::Wood){Wood=*It;break;}
  if(!Wood){Finish(false,TEXT("No wood node"));return;}
  FRTSOrder Order;Order.Type=ERTSOrderType::Gather;Order.TargetActor=Wood;Worker->ReceiveOrder(Order);Stage=5;return;
 }
 if(Stage==5)
 {
  if(Campaign->GetGuideStep()<4)return;
  if(!bGuideWoodVerified){if(Stock->GetAmount(ERTSResource::Wood)<510){Finish(false,TEXT("No actual wood delivery"));return;}bGuideWoodVerified=true;}
  if(!EnsureBuilt(Campaign,TEXT("ResearchInstitute")))return;
  Campaign->PrepareSave();Campaign->SelectServiceBuilding(TEXT("ResearchInstitute"));
  if(!Campaign->Upgrade(TEXT("Swordman"))){Finish(false,Campaign->GetMessage());return;}
  Stage=6;return;
 }
 if(Stage==6)
 {
  if(Campaign->GetGuideStep()<5)return;
  if(!EnsureBuilt(Campaign,TEXT("Barracks")))return;
  Campaign->SelectServiceBuilding(TEXT("Barracks"));
  if(!Campaign->Train(TEXT("Swordman"))){Finish(false,Campaign->GetMessage());return;}
  Stage=7;return;
 }
 if(Stage==7)
 {
  if(Campaign->GetGuideStep()<6)return;
  Campaign->NotifyPageOpened(0);Campaign->AdvanceGuide();Campaign->PrepareSave();
  Attacker=Campaign->Spawn(TEXT("Swordman"),0,Campaign->HomeLocation()+FVector(-2000,1800,0));
  Target=Campaign->Spawn(TEXT("Swordman"),1,Campaign->HomeLocation()+FVector(-1700,1800,0),15,0.01f);
  if(!Attacker.IsValid() || !Target.IsValid()){Finish(false,TEXT("Combat pair spawn failed"));return;}
  InitialTargetHealth=Target->GetHealth()->GetHealth();FRTSOrder Attack;Attack.Type=ERTSOrderType::Attack;Attack.TargetActor=Target;Attacker->ReceiveOrder(Attack);
  Stage=8;return;
 }
 if(Stage==8)
 {
  if(Target.IsValid() && Target->IsAlive())return;
  if(!Attacker.IsValid() || !Attacker->IsAlive()){Finish(false,TEXT("Combat attacker did not survive"));return;}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE combat defeated target hp=%.1f"),InitialTargetHealth);
  Campaign->PrepareSave();Phase=Now;Stage=80;return;
 }
 if(Stage==80)
 {
  if(Now-Phase<11)return;
  auto* PC=Cast<ARTSHumanController>(World->GetFirstPlayerController());if(!PC || !Worker.IsValid()){Finish(false,TEXT("Worker construction input missing"));return;}
  PC->SelectBuilding(nullptr);PC->GetSelection()->SelectSingle(Worker.Get(),false);
  FrontlinePosition=Campaign->HomeLocation()+FVector(6000,-6000,0);
  const int32 Wood=Stock->GetAmount(ERTSResource::Wood);
  TArray<UUserWidget*> HUDs;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,HUDs,URTSCampaignWidget::StaticClass(),true);
  auto* HUD=HUDs.Num()?Cast<URTSCampaignWidget>(HUDs[0]):nullptr;
  if(!HUD){Finish(false,TEXT("Worker HUD missing"));return;}HUD->RefreshSelectedObject();
  auto* WorkerPanel=HUD->WidgetTree->FindWidget(TEXT("WorkerOnlyActions"));
  if(!WorkerPanel || WorkerPanel->GetVisibility()!=ESlateVisibility::Visible){Finish(false,TEXT("Worker construction panel not visible"));return;}
  if(!Click(HUD,TEXT("BuildFrontlineBarracks")) || !PC->IsPlacingBuilding()){Finish(false,TEXT("Worker build button did not enter placement"));return;}
  PC->CancelBuildingPlacement();if(Stock->GetAmount(ERTSResource::Wood)!=Wood){Finish(false,TEXT("Placement cancel charged resources"));return;}
  PC->BeginBuildingPlacement(TEXT("Barracks"));
  if(PC->ConfirmBuildingPlacement(FVector(RTSCampaignMapSpace::MaxX+1000,0,0)) || Stock->GetAmount(ERTSResource::Wood)!=Wood){Finish(false,TEXT("Invalid placement charged resources"));return;}
  if(!PC->ConfirmBuildingPlacement(FrontlinePosition)){Finish(false,PC->GetEconomyMessage());return;}
  for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->GetBuildingId()==TEXT("Barracks") && FVector::Dist2D(It->GetActorLocation(),FrontlinePosition)<10)Frontline=*It;
  const auto* Row=Balance->Find(TEXT("Barracks"));
  if(!Frontline.IsValid() || Frontline->IsComplete() || !Row || Stock->GetAmount(ERTSResource::Wood)!=Wood-Row->WoodCost){Finish(false,TEXT("Barracks not paid unfinished worker construction"));return;}
  PC->SelectBuilding(Frontline.Get());FString Error;if(Campaign->CanTrain(TEXT("Swordman"),Error)){Finish(false,TEXT("Unfinished Barracks trained units"));return;}
  Stage=81;Phase=Now;return;
 }
 if(Stage==81)
 {
  if(!Frontline.IsValid()){Finish(false,TEXT("Construction site lost"));return;}
  bConstructionObserved|=Frontline->GetBuildProgress()>0;
  if(!Frontline->IsComplete())return;
  if(!bConstructionObserved){Finish(false,TEXT("No real worker construction progress"));return;}
  auto* PC=Cast<ARTSHumanController>(World->GetFirstPlayerController());PC->SelectBuilding(Frontline.Get());
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->IsAlive() && It->GetTeam()==0)++BeforeFrontlineTraining;
  if(!Campaign->Train(TEXT("Swordman")) || Frontline->GetProducer()->GetQueue().Num()!=1){Finish(false,TEXT("Selected frontline queue not used"));return;}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE frontline constructed at=%s"),*FrontlinePosition.ToString());
  Stage=82;return;
 }
 if(Stage==82)
 {
  if(!Frontline.IsValid() || !Frontline->GetProducer()->GetQueue().IsEmpty())return;
  int32 Alive=0;bool Nearby=false;for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->IsAlive() && It->GetTeam()==0){++Alive;Nearby|=It->GetUnitData()->BalanceId==TEXT("Swordman") && FVector::Dist2D(It->GetActorLocation(),FrontlinePosition)<1200;}
  if(Alive<=BeforeFrontlineTraining || !Nearby){Finish(false,TEXT("No actual soldier produced at frontline"));return;}
  Campaign->PrepareSave();Phase=Now;Stage=9;return;
 }
 if(Stage==9)
 {
  if(Now-Phase<11)return;
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->IsAlive() && (It->IsInTrafficPass() || It->IsYieldingToTraffic() || It->GetAttack()->IsSwinging() || It->GetCurrentOrder().Type==ERTSOrderType::Attack))
  {if(Now-Phase>60)Finish(false,TEXT("Save readiness timeout: combat/traffic still active before pause"));return;}
  TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,URTSCampaignWidget::StaticClass(),true);
  auto* HUD=Widgets.Num()?Cast<URTSCampaignWidget>(Widgets[0]):nullptr;
  if(!HUD){Finish(false,TEXT("Campaign HUD missing for settings"));return;}
  HUD->ShowSettings();auto* Settings=Cast<URTSCampaignSettingsWidget>(HUD->WidgetTree->FindWidget(TEXT("CampaignSettings")));
  if(!Settings || !Settings->IsOpen() || !UGameplayStatics::IsGamePaused(this)){Finish(false,TEXT("Gear settings did not pause"));return;}
  auto Change=[&](const TCHAR* Name,float Value){auto* Slider=Cast<USlider>(Settings->WidgetTree->FindWidget(Name));if(Slider)Slider->OnValueChanged.Broadcast(Value);return Slider!=nullptr;};
  if(!Change(TEXT("MasterVolume"),.6f) || !Change(TEXT("MusicVolume"),.25f) || !Change(TEXT("EffectsVolume"),.4f)){Finish(false,TEXT("Volume controls missing"));return;}
  // Save now uses a separate page, followed by explicit save/overwrite consent.
  if(!Click(Settings,TEXT("SaveCampaign")) || !Click(Settings,TEXT("SaveToSlot1")) || !Click(Settings,TEXT("CancelSaveConfirmation")) || !Settings->IsOpen()){Finish(false,TEXT("Save confirmation cancel failed"));return;}
  if(!Click(Settings,TEXT("SaveToSlot1")) || !Click(Settings,TEXT("ConfirmSave")) || Settings->IsOpen() || !UGameplayStatics::DoesSaveGameExist(*ARTSCampaign::SaveSlotName(),0)){Finish(false,FString::Printf(TEXT("Confirmed save did not resume gameplay: open=%d exists=%d reason=%s"),Settings->IsOpen(),UGameplayStatics::DoesSaveGameExist(*ARTSCampaign::SaveSlotName(),0),*Campaign->GetMessage()));return;}
  HUD->ShowSettings();
  SavedWood=Stock->GetAmount(ERTSResource::Wood);Stock->Add(ERTSResource::Wood,7);
  if(!Click(Settings,TEXT("LoadCampaign")) || !Click(Settings,TEXT("LoadSlot1")) || !Click(Settings,TEXT("CancelLoad")) || Stock->GetAmount(ERTSResource::Wood)!=SavedWood+7){Finish(false,TEXT("Load cancel changed progress"));return;}
  if(!Click(Settings,TEXT("LoadCampaign")) || !Click(Settings,TEXT("LoadSlot1")) || !Click(Settings,TEXT("ConfirmLoad")) || Stock->GetAmount(ERTSResource::Wood)!=SavedWood || Campaign->GetLevel(TEXT("Swordman"))!=2){Finish(false,TEXT("Settings save/load roundtrip mismatch"));return;}
  int32 Restored=0;for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->IsAlive() && It->GetBuildingId()==TEXT("Barracks") && FVector::Dist2D(It->GetActorLocation(),FrontlinePosition)<10)++Restored;
  if(Restored!=1){Finish(false,TEXT("Additional Barracks save/load duplicated or lost"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/StandaloneSettings.png"),true,false);Stage=95;Phase=Now;return;
 }
 if(Stage==95)
 {
  if(Now-Phase<1)return;
  TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,URTSCampaignWidget::StaticClass(),true);
  auto* HUD=Widgets.Num()?Cast<URTSCampaignWidget>(Widgets[0]):nullptr;auto* Settings=HUD?Cast<URTSCampaignSettingsWidget>(HUD->WidgetTree->FindWidget(TEXT("CampaignSettings"))):nullptr;
  if(!Settings){Finish(false,TEXT("Settings vanished"));return;}Settings->CloseSettings();
  if(UGameplayStatics::IsGamePaused(this)){Finish(false,TEXT("Settings close did not resume"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/StandaloneCampaign.png"),true,false);
  Phase=Now;Stage=96;return;
 }
 if(Stage==96)
 {
  if(Now-Phase<1)return;
  // Explicit automation fixture budget; never runs in Editor or Shipping gameplay.
  Stock->Add(ERTSResource::Wood,1000);Stock->Add(ERTSResource::Food,1000);
  TArray<ARTSUnit*> Workers;for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData()->bWorker)Workers.Add(*It);
  if(Workers.Num()<2){Finish(false,TEXT("Busy snapshot workers unavailable"));return;}
  ARTSUnit* Builder=nullptr;double Best=TNumericLimits<double>::Max();const FVector SiteAt=Campaign->HomeLocation()+FVector(4000,-6000,0);
  for(auto* U:Workers){const double D=FVector::DistSquared2D(U->GetActorLocation(),SiteAt);if(D<Best){Best=D;Builder=U;}}
  ARTSResourceNode* Wood=nullptr;double NodeDistance=TNumericLimits<double>::Max();
  for(TActorIterator<ARTSResourceNode> It(World);It;++It)if(It->GetResourceType()==ERTSResource::Wood && It->CanGather(0) && It->GetRemaining()>0)
  {const double D=FVector::DistSquared2D(It->GetActorLocation(),Campaign->HomeLocation());if(D<NodeDistance){NodeDistance=D;Wood=*It;}}
  BusyGatherer.Reset();Best=TNumericLimits<double>::Max();for(auto* U:Workers)if(U!=Builder && Wood){const double D=FVector::DistSquared2D(U->GetActorLocation(),Wood->GetActorLocation());if(D<Best){Best=D;BusyGatherer=U;}}
  if(!Wood || !BusyGatherer.IsValid()){Finish(false,TEXT("Busy gathering fixture unavailable"));return;}
  URTSOrderLibrary::IssueWorkOrder({BusyGatherer.Get()},Wood);
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE busy_gather worker=%s at=%s node=%s"),*BusyGatherer->GetName(),*BusyGatherer->GetActorLocation().ToString(),*Wood->GetActorLocation().ToString());
  Worker=Builder;Phase=Now;Stage=97;return;
 }
 if(Stage==97)
 {
  if(!BusyGatherer.IsValid() || !BusyGatherer->IsAlive() || !Worker.IsValid() || !Worker->IsAlive()){Finish(false,TEXT("Busy fixture worker lost"));return;}
  if(BusyGatherer->GetGatherer()->GetCarriedAmount()<2)
  {
   if(Now-Phase>60)
   {
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation StartNav;
    const bool OnNav=Nav && Nav->ProjectPointToNavigation(BusyGatherer->GetActorLocation(),StartNav,FVector(70,70,160));
    Finish(false,FString::Printf(TEXT("Busy gather timeout worker=%s location=%s order=%d cargo=%d status=%s tick=%d controller=%s onNav=%d navBuilding=%d paused=%d faction=%d"),*BusyGatherer->GetName(),*BusyGatherer->GetActorLocation().ToString(),int32(BusyGatherer->GetCurrentOrder().Type),BusyGatherer->GetGatherer()->GetCarriedAmount(),*BusyGatherer->GetGatherer()->GetStatus(),BusyGatherer->GetGatherer()->IsComponentTickEnabled(),*GetNameSafe(BusyGatherer->GetController()),OnNav,Nav && Nav->IsNavigationBuildInProgress(),UGameplayStatics::IsGamePaused(this),Campaign->GetActorFaction(BusyGatherer.Get())));
   }
   return;
  }
  FString SiteError;BusySite=Balance->PlaceBuilding(TEXT("Barracks"),0,Campaign->HomeLocation()+FVector(4000,-6000,0),SiteError);
  if(!BusySite.IsValid()){Finish(false,SiteError);return;}URTSOrderLibrary::IssueWorkOrder({Worker.Get()},BusySite.Get());
  auto* PC=Cast<ARTSHumanController>(World->GetFirstPlayerController());ARTSBuilding* Producer=nullptr;
  for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->IsAlive() && It->IsComplete() && It->GetBuildingId()==TEXT("Barracks") && FVector::Dist2D(It->GetActorLocation(),FrontlinePosition)<10)Producer=*It;
  if(!PC || !Producer){Finish(false,TEXT("Busy producer missing"));return;}PC->SelectBuilding(Producer);
  if(!Campaign->Train(TEXT("Swordman")) || !Campaign->Train(TEXT("Swordman"))){Finish(false,Campaign->GetMessage());return;}
  Phase=Now;Stage=98;return;
 }
 if(Stage==98)
 {
  if(Now-Phase<.1)return;
  if(!BusyGatherer.IsValid() || !BusySite.IsValid() || BusySite->IsComplete()){Finish(false,TEXT("Busy fixture construction completed too soon"));return;}
  if(!Campaign->SaveCampaign()){if(Now-Phase>10)Finish(false,Campaign->GetMessage());return;}
  BusyCargo=BusyGatherer->GetGatherer()->GetCarriedAmount();BusyBuild=BusySite->GetBuildProgress();
  if(BusyCargo<=0){Finish(false,TEXT("Busy save did not contain carried resources"));return;}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_SMOKE busy_saved cargo=%d build=%.3f queue=2"),BusyCargo,BusyBuild);
  if(!Campaign->LoadCampaign()){Finish(false,Campaign->GetMessage());return;}
  if(!Campaign->SaveCampaign()){Finish(false,Campaign->GetMessage());return;} // backup also contains the busy snapshot
  // Freeze only the live world while returning; the saved work states are untouched.
  UGameplayStatics::SetGamePaused(this,true);Stage=10;Phase=Now;return;
 }
 if(Stage==10)
 {
  if(Now-Phase<1)return;
  TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,URTSCampaignWidget::StaticClass(),true);
  if(Widgets.IsEmpty()){Finish(false,TEXT("Campaign HUD missing"));return;}
  auto* HUD=CastChecked<URTSCampaignWidget>(Widgets[0]);if(!UGameplayStatics::IsGamePaused(this))HUD->TogglePause();HUD->RequestReturnToMenu();HUD->CancelReturnToMenu();
  if(!UGameplayStatics::IsGamePaused(this)){Finish(false,TEXT("Pause failed"));return;}
  HUD->RequestReturnToMenu();HUD->ConfirmReturnToMenu();Stage=11;return;
 }
#endif
}
