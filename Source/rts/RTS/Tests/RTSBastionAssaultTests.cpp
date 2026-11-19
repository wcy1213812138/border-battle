#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Campaign/RTSCampaign.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSAttackComponent.h"
#include "AIController.h"
#include "Tests/RTSAutomationSaveScope.h"

namespace
{
 // Real campaign bases, real enemy buildings. Each kind gets one disclosed fast-cadence
 // attacker fixture; the buildings keep their real health, collision, guards and terrain.
 class FCheckCampaignBastionAssault : public IAutomationLatentCommand
 {
  FAutomationTestBase* Test;double Started=FPlatformTime::Seconds();
  TArray<TWeakObjectPtr<ARTSBuilding>> Targets;TArray<FName> Kinds;int32 Index=0;
  TWeakObjectPtr<ARTSUnit> Soldier;TWeakObjectPtr<ARTSBuilding> Current;float OriginalHP=0;double Phase=0;bool bDamaged=false;
  FVector Center=FVector::ZeroVector;int32 DamagedCount=0;int32 DestroyedCount=0;bool bCurrentDamaged=false;
  FString Detail;
 public:
  explicit FCheckCampaignBastionAssault(FAutomationTestBase* T):Test(T){}
  bool Update() override
  {
   auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;
   if(FPlatformTime::Seconds()-Started>420){Test->AddError(FString::Printf(TEXT("Campaign bastion assault timeout index=%d %s"),Index,*Detail));return true;}
   if(!W || !W->HasBegunPlay())return false;
   auto* C=ARTSCampaign::Get(W);if(!C || !C->IsReady())return false;
   auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav || Nav->IsNavigationBuildInProgress())return false;
   if(Targets.IsEmpty())
   {
    // One representative of every enemy building kind that actually exists in the campaign.
    TSet<FName> Seen;
    for(TActorIterator<ARTSBuilding> It(W);It;++It)
    {
     if(!It->IsAlive() || It->GetTeam()!=1 || C->GetActorFaction(*It)<0)continue;
     const FName Kind=It->GetBuildingId();if(Seen.Contains(Kind))continue;
     Seen.Add(Kind);Targets.Add(*It);Kinds.Add(Kind);
    }
    if(!Test->TestTrue(TEXT("Campaign has enemy buildings of several kinds"),Targets.Num()>=3))return true;
    return false;
   }
   if(Index>=Targets.Num())
   {
   Test->TestTrue(TEXT("Every enemy building kind took real weapon damage"),DamagedCount==Targets.Num());
   Test->TestTrue(TEXT("Every enemy building kind was destroyed"),DestroyedCount==Targets.Num());
   Test->AddInfo(FString::Printf(TEXT("CAMPAIGN_BASTION_ASSAULT_OK kinds=%d damaged=%d destroyed=%d detail=%s"),Targets.Num(),DamagedCount,DestroyedCount,*Detail));
    return true;
   }
  if(!Current.IsValid())
  {
   Current=Targets[Index];
   if(!Current.IsValid())
   {
    // A weak pointer only goes invalid once the actor was destroyed.
    if(bCurrentDamaged){++DestroyedCount;Detail+=FString::Printf(TEXT("%s=destroyed "),*Kinds[Index].ToString());}
    else{Detail+=FString::Printf(TEXT("[%s lostUndamaged]"),*Kinds[Index].ToString());Test->AddError(FString::Printf(TEXT("Enemy %s vanished without taking player damage"),*Kinds[Index].ToString()));return true;}
    ++Index;return false;
   }
   OriginalHP=Current->GetHealth()->GetHealth();bCurrentDamaged=false;
   Center=Current->GetActorLocation();
    FVector Feet=FVector::ZeroVector;bool bFound=false;
    for(int32 R=300;R<=900 && !bFound;R+=100)for(int32 A=0;A<16 && !bFound;++A)
    {
     const double Angle=A*2*UE_PI/16;FVector Candidate=Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*R;FNavLocation P;
     if(!Nav->ProjectPointToNavigation(Candidate,P,FVector(30,30,150)))continue;
     FCollisionQueryParams Q(SCENE_QUERY_STAT(BastionStart),false);
     Q.AddIgnoredActor(Current.Get());
     if(W->OverlapBlockingTestByChannel(P.Location+FVector(0,0,82),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(42,78),Q))continue;
     Feet=P.Location;bFound=true;
    }
    if(!bFound){Detail+=FString::Printf(TEXT("[%s no start]"),*Kinds[Index].ToString());Current.Reset();++Index;return false;}
    // Disclosed fixture: clear the base garrison so this measures building attackability,
    // not whether one lone unit can survive a defended base (see report notes).
    int32 Guards=0;
    {
     TArray<ARTSUnit*> Near;
     for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==1 && FVector::Dist2D(It->GetActorLocation(),Center)<2600)Near.Add(*It);
     for(auto* G:Near){G->Destroy();++Guards;}
    }
    auto* Data=URTSBalanceSubsystem::Get(W)->CreateUnitData(TEXT("Swordman"),W);
    Data->AttackDamage=200;Data->AttackInterval=.3f;Data->AttackHitDelay=.1f;Data->MaxHealth=5000;
    const FTransform UT(Feet+FVector(0,0,82));
    auto* U=W->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),UT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!U){Detail+=FString::Printf(TEXT("[%s no soldier]"),*Kinds[Index].ToString());Current.Reset();++Index;return false;}
    U->Configure(Data,0);U->FinishSpawning(UT);Soldier=U;
    // The campaign's level system rewrites unit stats after spawning; re-apply the
    // fixture values so this test measures the building, not progression scaling.
    U->GetUnitData()->AttackDamage=200;U->GetAttack()->SetStats(200,U->GetAttack()->GetRange(),.3f,.1f);
    U->GetHealth()->SetMaxHealth(5000);
    Test->AddInfo(FString::Printf(TEXT("BASTION_FIXTURE kind=%s guardsCleared=%d maxHp=%.1f damage=%.1f range=%.1f"),*Kinds[Index].ToString(),Guards,U->GetHealth()->GetMaxHealth(),U->GetUnitData()->AttackDamage,U->GetAttack()->GetRange()));
    auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());
    PC->GetSelection()->SelectSingle(U,false);
    const bool bAccepted=PC->RequestContextCommandAt(Current.Get(),Current->GetActorLocation(),false);
    if(!Test->TestTrue(FString::Printf(TEXT("Player can order an attack on enemy %s"),*Kinds[Index].ToString()),bAccepted))
    {Detail+=FString::Printf(TEXT("[%s orderRejected]"),*Kinds[Index].ToString());Current.Reset();Soldier->Destroy();++Index;return false;}
    Phase=W->GetTimeSeconds();bDamaged=false;return false;
   }
  if(!Current->IsAlive())
  {
   Test->TestTrue(FString::Printf(TEXT("Enemy %s was destroyed after taking player damage"),*Kinds[Index].ToString()),bCurrentDamaged);
   // Prove the building really left the world, not merely that its health hit zero.
   bool bStillStands=false;
   for(TActorIterator<ARTSBuilding> It(W);It;++It)
    if(It->GetBuildingId()==Kinds[Index] && FVector::Dist2D(It->GetActorLocation(),Center)<300)bStillStands=true;
   Test->TestFalse(FString::Printf(TEXT("Enemy %s is gone from the world"),*Kinds[Index].ToString()),bStillStands);
   ++DestroyedCount;Detail+=FString::Printf(TEXT("%s=destroyed "),*Kinds[Index].ToString());
   if(Soldier.IsValid())Soldier->Destroy();
   Current.Reset();bCurrentDamaged=false;++Index;return false;
  }
  if(Current->GetHealth()->GetHealth()<OriginalHP)
  {
   const float Now2=Current->GetHealth()->GetHealth();
   Test->AddInfo(FString::Printf(TEXT("BASTION_HIT kind=%s hp=%.1f from=%s soldierAlive=%d soldierHp=%.1f"),*Kinds[Index].ToString(),Now2,
    *GetNameSafe(Current->GetHealth()->GetLastDamageSource()),Soldier.IsValid()?1:0,Soldier.IsValid()?Soldier->GetHealth()->GetHealth():-1));
   if(!bCurrentDamaged)++DamagedCount;
   bCurrentDamaged=true;OriginalHP=Now2;bDamaged=true;
    Test->TestTrue(FString::Printf(TEXT("Damage on %s comes from the player soldier"),*Kinds[Index].ToString()),
     Current->GetHealth()->GetLastDamageSource()==Soldier.Get());
   }
   if(W->GetTimeSeconds()-Phase>130)
   {
    auto* U=Soldier.Get();auto* AI=U?Cast<AAIController>(U->GetController()):nullptr;
    Test->AddError(FString::Printf(TEXT("Enemy %s not destroyed: hp=%.1f damaged=%d edge=%.1f range=%.1f soldier=%s hp=%.1f attack=%.1f move=%d order=%d lastSource=%s"),
     *Kinds[Index].ToString(),Current->GetHealth()->GetHealth(),bDamaged,
     U?URTSAttackComponent::EdgeDistance2D(U,Current.Get()):-1,U?U->GetAttack()->GetRange():-1,
     U?*U->GetActorLocation().ToString():TEXT("destroyed"),U?U->GetHealth()->GetHealth():-1,
     U&&U->GetUnitData()?U->GetUnitData()->AttackDamage:-1,AI?int32(AI->GetMoveStatus()):-1,U?int32(U->GetCurrentOrder().Type):-1,
     *GetNameSafe(Current->GetHealth()->GetLastDamageSource())));
    return true;
   }
   return false;
  }
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignBastionAssaultTest,"MiniRTS.Combat.CampaignEnemyBuildingTargets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignBastionAssaultTest::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("Bastion"));
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckCampaignBastionAssault(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));
 return true;
}
#endif
