#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/InputComponent.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealthComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Match/RTSMatchSubsystem.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRoleSelectionTest,"MiniRTS.Input.WorkerArmySelectionHotkeys",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
namespace
{
 class FCheckRoleSelection : public IAutomationLatentCommand
 {
 public:
  explicit FCheckRoleSelection(FAutomationTestBase* InTest):Test(InTest){}
  bool Update() override
  {
   UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
   if(!W || !W->HasBegunPlay())
   {if(FPlatformTime::Seconds()-Started<30)return false;Test->AddError(TEXT("Role-selection PIE unavailable"));return true;}
   auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());auto* Balance=URTSBalanceSubsystem::Get(W);
   if(!PC || !PC->InputComponent || !Balance){Test->AddError(TEXT("Role-selection controller/data unavailable"));return true;}
   if(auto* Match=URTSMatchSubsystem::Get(W))Match->EnterSandbox();
   auto Spawn=[&](FName Kind,int32 Index,uint8 Team=0)
   {
    const FTransform Transform(FVector(-1400+Index*200,-1000,82));
    auto* U=W->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    U->Configure(Balance->CreateUnitData(Kind,W),Team);U->FinishSpawning(Transform);U->SetCombatStance(ERTSCombatStance::Guard);return U;
   };
   auto* Idle=Spawn(TEXT("Peasant"),0);auto* Busy=Spawn(TEXT("Peasant"),1);auto* Soldier=Spawn(TEXT("Swordman"),2);
   auto* Enemy=Spawn(TEXT("Peasant"),3,1);auto* Dead=Spawn(TEXT("Peasant"),4);Dead->GetHealth()->ApplyDamage(100000,nullptr);
   const FTransform NT(FVector(-200,800,90));auto* Node=W->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),NT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Node->Configure(ERTSResource::Wood,500);Node->FinishSpawning(NT);
   const FTransform BT(FVector(900,900,0));auto* Hall=W->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),BT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Hall->Configure(TEXT("TownHall"),0,true);Hall->FinishSpawning(BT);
   FRTSOrder Work;Work.Type=ERTSOrderType::Gather;Work.TargetActor=Node;Busy->ReceiveOrder(Work);
   auto Invoke=[&](FKey Key)
   {
    int32 Found=0;
    for(auto& Binding:PC->InputComponent->KeyBindings)
    {
     if(Binding.Chord.Key!=Key || !Binding.Chord.bAlt || Binding.Chord.bCtrl || Binding.Chord.bShift || Binding.Chord.bCmd || Binding.KeyEvent!=IE_Pressed)continue;
     if(Binding.KeyDelegate.IsBound()){++Found;Binding.KeyDelegate.Execute(Key);}
    }
    Test->TestEqual(TEXT("Exactly one Alt role key binding"),Found,1);
   };
   auto CheckRole=[&](bool Workers)
   {
    TArray<ARTSUnit*> Expected;
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData() && It->GetUnitData()->bWorker==Workers)Expected.Add(*It);
    const auto Selected=PC->GetSelection()->GetSelectedUnits();
    Test->TestEqual(TEXT("Role selection has exact count"),Selected.Num(),Expected.Num());
    for(auto* U:Expected)Test->TestTrue(TEXT("Every own living role member selected"),Selected.Contains(U));
    Test->TestFalse(TEXT("Enemy excluded"),Selected.Contains(Enemy));Test->TestFalse(TEXT("Dead excluded"),Selected.Contains(Dead));
    return Selected.Num();
   };
   PC->SelectBuilding(Hall);Invoke(EKeys::F1);const int32 WorkerCount=CheckRole(true);
   Test->TestNull(TEXT("Worker key clears selected building"),PC->GetSelectedBuilding());
   Test->TestTrue(TEXT("Idle and working workers both selected"),PC->GetSelection()->GetSelectedUnits().Contains(Idle) && PC->GetSelection()->GetSelectedUnits().Contains(Busy));
   Test->TestFalse(TEXT("Worker key excludes army"),Soldier->IsSelected());
   Invoke(EKeys::F1);CheckRole(true); // Repeated press must not toggle members off.
   Invoke(EKeys::F2);const int32 ArmyCount=CheckRole(false);
   Test->TestFalse(TEXT("Army key clears workers"),Idle->IsSelected() || Busy->IsSelected());
   Test->TestTrue(TEXT("Army key includes soldier"),Soldier->IsSelected());
   PC->SelectResource(Node);Invoke(EKeys::F1);CheckRole(true);
   Test->TestNull(TEXT("Role key clears resource context"),PC->GetSelectedResource());
   Test->TestTrue(TEXT("Selection preserves busy worker's real task"),Busy->GetCurrentOrder().Type==ERTSOrderType::Gather && Busy->GetCurrentOrder().TargetActor.Get()==Node);
   Test->TestTrue(TEXT("Selection does not order idle soldier"),Soldier->GetCurrentOrder().Type==ERTSOrderType::None);
   UGameplayStatics::SetGamePaused(W,true);Invoke(EKeys::F2);CheckRole(true);UGameplayStatics::SetGamePaused(W,false);
   PC->GetSelection()->OwningTeam=254;
   Test->TestEqual(TEXT("No matching workers returns zero"),PC->SelectAllWorkers(),0);
   Test->TestEqual(TEXT("No matching role clears old selection"),PC->GetSelection()->GetSelectedCount(),0);
   Test->TestTrue(TEXT("Empty worker feedback"),PC->GetEconomyMessage().Contains(TEXT("没有可选的工人")));
   PC->GetSelection()->OwningTeam=0;
   Invoke(EKeys::F2);CheckRole(false);
   Test->AddInfo(FString::Printf(TEXT("ROLE_SELECTION_HOTKEYS_OK workers=%d army=%d boundAltF1=1 boundAltF2=1 busyIncluded=1 exclusive=1 ordersPreserved=1 pausedUnchanged=1 emptyCleared=1 physicalKeyboardTest=0"),WorkerCount,ArmyCount));return true;
  }
 private:
  FAutomationTestBase* Test;double Started=FPlatformTime::Seconds();
 };
}
bool FRTSRoleSelectionTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckRoleSelection(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
