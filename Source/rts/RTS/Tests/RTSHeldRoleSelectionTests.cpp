#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Match/RTSMatchSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHeldRoleSelectionTest,"MiniRTS.Input.HeldRoleClickAndBox",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
namespace
{
 class FSelectionProjectionViewport : public FSceneViewport
 {
 public:
  FSelectionProjectionViewport():FSceneViewport(TSharedPtr<SViewport>()){}
  FIntPoint GetSizeXY() const override { return FIntPoint(1024,768); }
 };
 class FCheckHeldRoleSelection : public IAutomationLatentCommand
 {
 public:
  explicit FCheckHeldRoleSelection(FAutomationTestBase* T):Test(T){}
  bool Update() override
  {
   if(FPlatformTime::Seconds()-Started>35){Test->AddError(TEXT("Held-role selection watchdog"));return true;}
   UWorld* W=GEditor?GEditor->PlayWorld:nullptr;if(!W || !W->HasBegunPlay())return false;
   PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());auto* Balance=URTSBalanceSubsystem::Get(W);
   if(!PC || !PC->PlayerInput || !Balance){Test->AddError(TEXT("Held-role controller/input missing"));return true;}
   if(!bSpawned)
   {
    if(auto* Match=URTSMatchSubsystem::Get(W))Match->EnterSandbox();
    auto Spawn=[&](FName Kind,FVector P,uint8 Team=0)
    {
     const FTransform T(P);auto* U=W->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
     U->Configure(Balance->CreateUnitData(Kind,W),Team);U->FinishSpawning(T);U->SetCombatStance(ERTSCombatStance::Guard);return U;
    };
    Worker=Spawn(TEXT("Peasant"),FVector(-400,0,82));Busy=Spawn(TEXT("Peasant"),FVector(200,0,82));
    Soldier=Spawn(TEXT("Swordman"),FVector(-100,150,82));Outside=Spawn(TEXT("Swordman"),FVector(1800,0,82));Enemy=Spawn(TEXT("Peasant"),FVector(0,-200,82),1);
    Dead=Spawn(TEXT("Peasant"),FVector(-200,-200,82));Dead->GetHealth()->ApplyDamage(100000,nullptr);
    FTransform NT(FVector(1500,1000,90));Node=W->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),NT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Node->Configure(ERTSResource::Wood,500);Node->FinishSpawning(NT);
    FTransform BT(FVector(-1500,1000,0));Hall=W->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),BT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Hall->Configure(TEXT("TownHall"),0,true);Hall->FinishSpawning(BT);
    FRTSOrder Work;Work.Type=ERTSOrderType::Gather;Work.TargetActor=Node;Busy->ReceiveOrder(Work);
    auto* Camera=W->SpawnActor<ACameraActor>(FVector(0,0,2500),FRotator(-90,0,0));Camera->GetCameraComponent()->FieldOfView=90;Camera->GetCameraComponent()->bConstrainAspectRatio=false;PC->SetViewTarget(Camera);
    bSpawned=true;return false;
   }
   // NullRHI editor PIE has no sized viewport. Provide projection only: no window,
   // rendering or OS cursor. Restore the local player immediately after this check.
   auto* Local=PC->GetLocalPlayer();if(!Local){Test->AddError(TEXT("Projection local player missing"));return true;}
   auto PreviousClient=Local->ViewportClient;auto* Client=PreviousClient?PreviousClient.Get():NewObject<UGameViewportClient>();
   FSceneViewport* PreviousViewport=Client->Viewport;const FVector2D PreviousSize=Local->Size,PreviousOrigin=Local->Origin;
   FSelectionProjectionViewport ProjectionViewport;Local->ViewportClient=Client;Client->Viewport=&ProjectionViewport;Local->Size=FVector2D(1,1);Local->Origin=FVector2D::ZeroVector;
   ON_SCOPE_EXIT{Client->Viewport=PreviousViewport;Local->ViewportClient=PreviousClient;Local->Size=PreviousSize;Local->Origin=PreviousOrigin;};
   if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(0);
   auto Key=[&](FKey K,bool Down)
   {
    PC->PlayerInput->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,K,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,FPlatformTime::Cycles64()));
    PC->PlayerInput->ProcessInputStack({},.016f,false);
   };
   Key(EKeys::T,false);Key(EKeys::G,false);
   Test->TestTrue(TEXT("Released keys give ordinary selection"),PC->GetMouseSelectionFilter()==ERTSSelectionFilter::All);
   Key(EKeys::T,true);Test->TestTrue(TEXT("Held T resolves workers"),PC->GetMouseSelectionFilter()==ERTSSelectionFilter::Workers);
   Test->TestEqual(TEXT("Pressing filter alone does not select units"),PC->GetSelection()->GetSelectedCount(),0);
   PC->RequestMouseSelection(Worker,false,PC->GetMouseSelectionFilter());Check({Worker});
   PC->RequestMouseSelection(Busy,true,PC->GetMouseSelectionFilter());Check({Worker,Busy});
   PC->RequestMouseSelection(Soldier,true,PC->GetMouseSelectionFilter());Check({Worker,Busy});
   PC->RequestMouseSelection(Enemy,true,PC->GetMouseSelectionFilter());Check({Worker,Busy});
   PC->RequestMouseSelection(Dead,true,PC->GetMouseSelectionFilter());Check({Worker,Busy});
   PC->RequestMouseSelection(Worker,true,PC->GetMouseSelectionFilter());Check({Busy}); // Existing Shift click toggle.
   PC->RequestMouseSelection(Soldier,false,PC->GetMouseSelectionFilter());Check({});
   Key(EKeys::T,false);Key(EKeys::G,true);
   Test->TestTrue(TEXT("Held G resolves military"),PC->GetMouseSelectionFilter()==ERTSSelectionFilter::Military);
   PC->RequestMouseSelection(Soldier,false,PC->GetMouseSelectionFilter());Check({Soldier});
   PC->RequestMouseSelection(Worker,true,PC->GetMouseSelectionFilter());Check({Soldier});
   Key(EKeys::T,true);Test->TestTrue(TEXT("Both held safely select none"),PC->GetMouseSelectionFilter()==ERTSSelectionFilter::None);
   PC->RequestMouseSelection(Worker,false,PC->GetMouseSelectionFilter());Check({});Key(EKeys::G,false);
   FVector2D A(1e9,1e9),B(-1e9,-1e9);
   for(auto* U:{Worker,Busy,Soldier,Enemy})
   {
    FVector2D Screen;if(!PC->ProjectWorldLocationToScreen(U->GetActorLocation(),Screen,true))
    {auto* LP=PC->GetLocalPlayer();auto* V=LP && LP->ViewportClient?LP->ViewportClient->Viewport:nullptr;Test->AddError(FString::Printf(TEXT("Cannot project rectangle fixture: viewport=%d size=%s camera=%s"),V!=nullptr,V?*V->GetSizeXY().ToString():TEXT("none"),*PC->PlayerCameraManager->GetCameraLocation().ToCompactString()));return true;}
    A=FVector2D::Min(A,Screen);B=FVector2D::Max(B,Screen);
   }
   A-=FVector2D(20,20);B+=FVector2D(20,20);
   FVector2D FarScreen;PC->ProjectWorldLocationToScreen(Outside->GetActorLocation(),FarScreen,true);
   if(!Test->TestFalse(TEXT("Outside soldier is geometrically outside box"),FBox2D(A,B).IsInside(FarScreen)))return true;
   PC->RequestBoxSelection(A,B,false,PC->GetMouseSelectionFilter());Check({Worker,Busy});
   Key(EKeys::T,false);Key(EKeys::G,true);
   PC->RequestBoxSelection(B,A,false,PC->GetMouseSelectionFilter());Check({Soldier}); // Reverse drag.
   PC->RequestMouseSelection(Outside,true,PC->GetMouseSelectionFilter());Check({Soldier,Outside});
   PC->RequestBoxSelection(A,B,true,PC->GetMouseSelectionFilter());Check({Soldier,Outside});
   Key(EKeys::G,false);Key(EKeys::T,true);
   PC->RequestBoxSelection(A,B,true,PC->GetMouseSelectionFilter());Check({Worker,Busy}); // Prune other role even with Shift.
   Key(EKeys::T,false);
   PC->RequestBoxSelection(A,B,false,PC->GetMouseSelectionFilter());Check({Worker,Busy,Soldier});
   PC->RequestMouseSelection(Hall,false,ERTSSelectionFilter::All);Test->TestTrue(TEXT("Ordinary building selection preserved"),PC->GetSelectedBuilding()==Hall);
   PC->RequestMouseSelection(Hall,false,ERTSSelectionFilter::Workers);Check({});Test->TestNull(TEXT("Filtered click cannot select building"),PC->GetSelectedBuilding());
   PC->RequestMouseSelection(Node,false,ERTSSelectionFilter::All);Test->TestTrue(TEXT("Ordinary resource selection preserved"),PC->GetSelectedResource()==Node);
   PC->RequestBoxSelection(A,B,false,ERTSSelectionFilter::Workers);Check({Worker,Busy});Test->TestNull(TEXT("Box clears stale resource context"),PC->GetSelectedResource());
   UGameplayStatics::SetGamePaused(W,true);Test->TestFalse(TEXT("Paused selection blocked"),PC->RequestBoxSelection(A,B,false,ERTSSelectionFilter::Military));Check({Worker,Busy});UGameplayStatics::SetGamePaused(W,false);
   Test->TestTrue(TEXT("Busy worker task unchanged"),Busy->GetCurrentOrder().Type==ERTSOrderType::Gather && Busy->GetCurrentOrder().TargetActor.Get()==Node);
   Test->AddInfo(TEXT("HELD_ROLE_SELECTION_OK keyStateTG=1 click=1 projectedBox=1 outsideExcluded=1 noAutoSelect=1 shift=1 reverseBox=1 releaseOrdinary=1 busyIncluded=1 tasksUnchanged=1 physicalInputTest=0"));return true;
  }
 private:
  void Check(const TArray<ARTSUnit*>& Expected)
  {
   const auto Selected=PC->GetSelection()->GetSelectedUnits();Test->TestEqual(TEXT("Exact filtered selection count"),Selected.Num(),Expected.Num());
   for(auto* U:Expected)Test->TestTrue(TEXT("Expected filtered member present"),Selected.Contains(U));
  }
  FAutomationTestBase* Test;ARTSHumanController* PC=nullptr;ARTSUnit* Worker=nullptr;ARTSUnit* Busy=nullptr;ARTSUnit* Soldier=nullptr;ARTSUnit* Outside=nullptr;ARTSUnit* Enemy=nullptr;ARTSUnit* Dead=nullptr;
  ARTSResourceNode* Node=nullptr;ARTSBuilding* Hall=nullptr;bool bSpawned=false;double Started=FPlatformTime::Seconds();
 };
}
bool FRTSHeldRoleSelectionTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckHeldRoleSelection(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
