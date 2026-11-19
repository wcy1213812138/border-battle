#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSOrderLibrary.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"

// Shared right-click entry, not physical desktop input. Movement uses real nav,
// AI and character collision; actors are never repositioned after spawning.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSContextMotionTest,"MiniRTS.Input.ContextCommandRealMovement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
namespace
{
 class FCheckContextMotion : public IAutomationLatentCommand
 {
 public:
  explicit FCheckContextMotion(FAutomationTestBase* T):Test(T){}
  bool Update() override
  {
   if(FPlatformTime::Seconds()-Started>65){Test->AddError(FString::Printf(TEXT("Context command watchdog at stage %d"),Stage));return true;}
   UWorld* W=GEditor?GEditor->PlayWorld:nullptr;if(!W || !W->HasBegunPlay())return false;
   const double Now=W->GetTimeSeconds();
   if(Stage==0)
   {
    PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());auto* Balance=URTSBalanceSubsystem::Get(W);
    if(!PC || !Balance)return false;
    if(auto* M=URTSMatchSubsystem::Get(W))M->EnterSandbox();
    auto Spawn=[&](FName Kind,FVector P,uint8 Team=0)
    {
     const FTransform T(FRotator(0,180,0),P);auto* U=W->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
     U->Configure(Balance->CreateUnitData(Kind,W),Team);U->FinishSpawning(T);U->SetCombatStance(ERTSCombatStance::Guard);return U;
    };
    Single=Spawn(TEXT("Swordman"),FVector(-1200,0,82));Worker=Spawn(TEXT("Peasant"),FVector(-1200,900,82));Enemy=Spawn(TEXT("Swordman"),FVector(1500,1700,82),1);
    for(int32 I=0;I<3;++I)Group.Add(Spawn(TEXT("Swordman"),FVector(-1200,-900-I*200,82)));
    FTransform NT(FVector(600,0,90));Node=W->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),NT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Node->Configure(ERTSResource::Wood,500);Node->FinishSpawning(NT);
    FTransform BT(FVector(600,-1100,0));Hall=W->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),BT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Hall->Configure(TEXT("TownHall"),0,true);Hall->FinishSpawning(BT);
    Radius=Single->GetCapsuleComponent()->GetScaledCapsuleRadius();At=Now;Stage=1;return false;
   }
   if(Stage==1)
   {
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(Now-At<2 || !Nav || Nav->IsNavigationBuildInProgress())return false;
    PC->GetSelection()->SelectSingle(Single,false);SingleStart=Single->GetActorLocation();
    if(!Test->TestTrue(TEXT("Soldier resource click accepts movement"),PC->RequestContextCommandAt(Node,Node->GetActorLocation()+FVector(0,0,80),false)))return true;
    Test->TestTrue(TEXT("Nonworker receives Move not ignored work"),Single->GetCurrentOrder().Type==ERTSOrderType::Move && Single->GetMoveGroup()!=nullptr);
    At=Now;Stage=2;return false;
   }
   if(Stage==2)
   {
    const double Moved=FVector::Dist2D(SingleStart,Single->GetActorLocation());
    if(Moved>250)
    {
     Test->TestTrue(TEXT("Soldier turns toward movement"),Single->GetActorForwardVector().X>.5f);
     Test->TestEqual(TEXT("Body radius unchanged"),Single->GetCapsuleComponent()->GetScaledCapsuleRadius(),Radius);
     Test->TestTrue(TEXT("Ordinary blocking collision unchanged"),Single->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block && Single->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldStatic)==ECR_Block && Single->GetCapsuleComponent()->GetMoveIgnoreActors().IsEmpty());
     URTSOrderLibrary::IssueStopOrder({Single});PC->GetSelection()->ClearSelection();
     for(auto* U:Group){PC->GetSelection()->SelectSingle(U,true);GroupStarts.Add(U->GetActorLocation());}
     if(!Test->TestTrue(TEXT("Whole group building click accepts movement"),PC->RequestContextCommandAt(Hall,Hall->GetActorLocation()+FVector(0,0,180),false)))return true;
     for(auto* U:Group)Test->TestTrue(TEXT("Each soldier gets Move at a friendly dropoff"),U->GetCurrentOrder().Type==ERTSOrderType::Move && U->GetMoveGroup()!=nullptr);
     Test->AddInfo(FString::Printf(TEXT("CONTEXT_SINGLE_MOVED cm=%.1f turned=1"),Moved));At=Now;Stage=3;return false;
    }
    if(Now-At>15){Test->AddError(FString::Printf(TEXT("Single did not move: start=%s current=%s order=%d"),*SingleStart.ToCompactString(),*Single->GetActorLocation().ToCompactString(),int32(Single->GetCurrentOrder().Type)));return true;}return false;
   }
   if(Stage==3)
   {
    double Minimum=1e9;for(int32 I=0;I<Group.Num();++I)Minimum=FMath::Min(Minimum,FVector::Dist2D(GroupStarts[I],Group[I]->GetActorLocation()));
    if(Minimum<=250)
    {if(Now-At>20){Test->AddError(FString::Printf(TEXT("Group did not move: min displacement %.1f cm"),Minimum));return true;}return false;}
    URTSOrderLibrary::IssueStopOrder(Group);
    PC->GetSelection()->SelectSingle(Worker,false);PC->GetSelection()->SelectSingle(Single,true);
    Test->TestTrue(TEXT("Mixed resource command accepted"),PC->RequestContextCommandAt(Node,Node->GetActorLocation(),false));
    Test->TestTrue(TEXT("Worker retains Gather"),Worker->GetCurrentOrder().Type==ERTSOrderType::Gather && Worker->GetCurrentOrder().TargetActor.Get()==Node);
    Test->TestTrue(TEXT("Mixed soldier moves, never receives worker-only order"),Single->GetCurrentOrder().Type==ERTSOrderType::Move);
    PC->GetSelection()->SelectSingle(Single,false);
    Test->TestTrue(TEXT("Plain ground movement accepted"),PC->RequestContextCommandAt(nullptr,FVector(-200,900,10),false));
    const FVector Current=Single->GetCurrentOrder().TargetLocation;
    Test->TestTrue(TEXT("Shift appends movement"),PC->RequestContextCommandAt(nullptr,FVector(200,1300,10),true));
    Test->TestTrue(TEXT("Shift preserves current command"),Single->GetCurrentOrder().TargetLocation.Equals(Current,.01) && Single->GetQueuedOrderCount()==1);
    UGameplayStatics::SetGamePaused(W,true);
    Test->TestFalse(TEXT("Paused context command rejected"),PC->RequestContextCommandAt(nullptr,FVector(0,0,10),false));
    UGameplayStatics::SetGamePaused(W,false);
    Test->TestFalse(TEXT("Unwalkable location rejected"),PC->RequestContextCommandAt(nullptr,FVector(1e7,1e7,1e7),false));
    Test->TestTrue(TEXT("Rejected location preserves queue"),Single->GetCurrentOrder().TargetLocation.Equals(Current,.01) && Single->GetQueuedOrderCount()==1);
    FHitResult EnemyHit;const FVector EnemyPoint=Enemy->GetActorLocation();
    Test->TestTrue(TEXT("Real command trace finds a visible Pawn despite Visibility-ignore profile"),PC->TraceContextTarget(EnemyPoint+FVector(0,0,500),EnemyPoint-FVector(0,0,500),EnemyHit) && EnemyHit.GetActor()==Enemy);
    FHitResult OccludedHit;
    Test->TestTrue(TEXT("Ground occludes Pawn from below"),PC->TraceContextTarget(EnemyPoint-FVector(0,0,500),EnemyPoint+FVector(0,0,500),OccludedHit) && OccludedHit.GetActor()!=Enemy);
    Test->TestTrue(TEXT("Traced hostile click accepts attack"),PC->RequestContextCommandAt(EnemyHit.GetActor(),EnemyHit.ImpactPoint,false));
    Test->TestTrue(TEXT("Attack target retained"),Single->GetCurrentOrder().Type==ERTSOrderType::Attack && Single->GetCurrentOrder().TargetActor.Get()==Enemy);
    // Visibility trace must stop at the terrain rather than object-query through it.
    FHitResult Visible;const bool Hit=W->LineTraceSingleByChannel(Visible,FVector(-300,300,500),FVector(-300,300,-500),ECC_Visibility);
    Test->TestTrue(TEXT("First visible ground surface is traceable"),Hit && Visible.bBlockingHit && !Visible.GetActor()->IsA<ARTSResourceNode>());
    URTSOrderLibrary::IssueStopOrder({Single,Worker});
    Test->AddInfo(FString::Printf(TEXT("CONTEXT_COMMAND_MOTION_OK singleMoved=1 groupMembers=3 groupMinCm=%.1f mixedWorkerGather=1 mixedSoldierMove=1 ground=1 queue=1 pause=1 noNav=1 attack=1 physicalMouseTest=0 collisionUnchanged=1"),Minimum));return true;
   }
   return false;
  }
 private:
  FAutomationTestBase* Test;ARTSHumanController* PC=nullptr;ARTSUnit* Single=nullptr;ARTSUnit* Worker=nullptr;ARTSUnit* Enemy=nullptr;ARTSResourceNode* Node=nullptr;ARTSBuilding* Hall=nullptr;
  TArray<ARTSUnit*> Group;TArray<FVector> GroupStarts;FVector SingleStart=FVector::ZeroVector;float Radius=0;int32 Stage=0;double Started=FPlatformTime::Seconds(),At=0;
 };
}
bool FRTSContextMotionTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckContextMotion(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
