#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "AIController.h"

namespace
{
 class FCheckBuildingAssault : public IAutomationLatentCommand
 {
  FAutomationTestBase* Test;FName Kind;double Started=FPlatformTime::Seconds();int32 Stage=0;
  TWeakObjectPtr<ARTSBuilding> Building;TWeakObjectPtr<ARTSUnit> Soldier;FVector Start;float OriginalHP=0;bool Damaged=false;
 public:
  FCheckBuildingAssault(FAutomationTestBase* T,FName K):Test(T),Kind(K){}
  bool Update() override
  {
   auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;
   if(FPlatformTime::Seconds()-Started>80 && Soldier.IsValid())if(auto* AI=Cast<AAIController>(Soldier->GetController()))Test->AddInfo(FString::Printf(TEXT("ASSAULT_PATH status=%d goal=%s velocity=%s order=%d"),int32(AI->GetMoveStatus()),*AI->GetImmediateMoveDestination().ToString(),*Soldier->GetVelocity().ToString(),int32(Soldier->GetCurrentOrder().Type)));
   if(FPlatformTime::Seconds()-Started>80){auto* U=Soldier.Get();auto* B=Building.Get();Test->AddError(FString::Printf(TEXT("Building assault stalled kind=%s hp=%.1f moved=%.1f edge=%.1f range=%.1f inRange=%d sight=%d unit=%s building=%s"),*Kind.ToString(),B?B->GetHealth()->GetHealth():-1,U?FVector::Dist2D(Start,U->GetActorLocation()):0,U && B?URTSAttackComponent::EdgeDistance2D(U,B):-1,U?U->GetAttack()->GetRange():-1,U && B?U->GetAttack()->IsInRange(B):0,U && B?U->GetAttack()->HasLineOfSight(B):0,U?*U->GetActorLocation().ToString():TEXT("none"),B?*B->GetActorLocation().ToString():TEXT("none")));return true;}
   if(!W || !W->HasBegunPlay())return false;
   auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);auto* Balance=URTSBalanceSubsystem::Get(W);
   if(!Nav || !Balance || Nav->IsNavigationBuildInProgress())return false;
   if(Stage==0)
   {
    FRTSEconomyScenario Scene;FString Error;if(!RTSEconomyScenario::Create(W,Scene,Error)){Test->AddError(Error);return true;}
    FVector Site;bool Found=false;
    for(int32 X=-1200;X<=1200 && !Found;X+=200)for(int32 Y=-1200;Y<=1200 && !Found;Y+=200)
    {FNavLocation P;if(Nav->ProjectPointToNavigation(FVector(X,Y,10),P,FVector(20,20,50)) && Balance->CanPlace(Kind,P.Location,Error)){Site=P.Location;Found=true;}}
    if(!Found){Test->AddError(TEXT("No enemy building test site"));return true;}
    const FTransform BT(FRotator(0,30,0),Site);auto* B=W->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),BT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    B->Configure(Kind,1,true);B->FinishSpawning(BT);Building=B;OriginalHP=B->GetHealth()->GetHealth();
    // Damage cadence is an explicit test fixture; building health/collision stay real.
    auto* Data=Balance->CreateUnitData(TEXT("Swordman"),W);Data->AttackDamage=200;Data->AttackInterval=.3f;Data->AttackHitDelay=.1f;
    FNavLocation P;if(!Nav->ProjectPointToNavigation(Site+FVector(0,850,0),P,FVector(30,30,100))){Test->AddError(TEXT("No attacker start"));return true;}
    const FTransform UT(P.Location+FVector(0,0,82));auto* U=W->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),UT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
    if(!U){Test->AddError(TEXT("Attacker start obstructed"));return true;}U->Configure(Data,0);U->FinishSpawning(UT);Soldier=U;Start=U->GetActorLocation();
    auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());PC->GetSelection()->SelectSingle(U,false);
    if(!Test->TestTrue(TEXT("Actual context attack accepts enemy building"),PC->RequestContextCommandAt(B,B->GetActorLocation(),false)))return true;
    Stage=1;return false;
   }
   if(Building.IsValid() && Building->IsAlive())
   {
    if(Building->GetHealth()->GetHealth()<OriginalHP){Damaged=true;Test->TestTrue(TEXT("Damage source is actual soldier"),Building->GetHealth()->GetLastDamageSource()==Soldier.Get());}
    return false;
   }
   Test->TestTrue(TEXT("Building took real weapon damage before death"),Damaged);
   Test->TestTrue(TEXT("Attacker physically approached building"),Soldier.IsValid() && FVector::Dist2D(Start,Soldier->GetActorLocation())>200);
   Test->AddInfo(FString::Printf(TEXT("BUILDING_ASSAULT_OK kind=%s rotated=1 actualWeaponDamage=1 destroyed=1 directHealthDamage=0 fixtureAttackCadence=1 physicalInput=0"),*Kind.ToString()));return true;
  }
 };
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSBuildingAssaultTest,"MiniRTS.Combat.BuildingAssault",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FRTSBuildingAssaultTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{for(const TCHAR* K:{TEXT("Barracks"),TEXT("TownHall"),TEXT("ArcheryRange")}){Names.Add(K);Commands.Add(K);}}
bool FRTSBuildingAssaultTest::RunTest(const FString& Kind)
{
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckBuildingAssault(this,FName(*Kind)));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
