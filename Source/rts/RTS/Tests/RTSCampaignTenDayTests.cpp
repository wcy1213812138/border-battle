// Real PIE soak with real mining, real defense and counter-sorties: no stock injection,
// snapshot edits, elapsed-time writes or enemy-order manipulation.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/RTSAutomationSaveScope.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSGathererComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Economy/RTSProducerComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignTenDayTest,
 "MiniRTS.Campaign.TenDay.RealPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace RTSTenDay
{
constexpr float GuardRadius=3200.f, RecallRadius=8000.f, MineClearRadius=2600.f, MineHoldRadius=680.f;

struct FDayReport
{
 int32 Upgrades=0,Trained=0,Peasants=0,GatherOrders=0,DefenseOrders=0,CounterOrders=0;
 int32 EnemyHits=0,FriendlyHits=0,PeakNear=0,PeakWorking=0,Delta[8]={0};
 float MinEnemyDistance=MAX_flt,HallMin=1.f;
 TSet<TWeakObjectPtr<ARTSUnit>> NearEnemies;
 bool Interactive() const { return DefenseOrders>0 || CounterOrders>0 || EnemyHits>0; }
};

class FRun : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FString Directory, Report;
 double Started=FPlatformTime::Seconds(), GuideStarted=0, LastPolicy=-100, LastSample=-1;
 int32 LoggedDays=0, InteractiveDays=0, LastMask=0, MinesTotal=0, SwordLevel=1, HallHits=0;
 bool Initialized=false, NavChecked=false, MoveSent=false, HallDamageSeen=false;
 int32 Towers=0, ExtraBarracks=0;
 TWeakObjectPtr<ARTSUnit> GuideWorker;
 TWeakObjectPtr<ARTSBuilding> Hall;
 TMap<TWeakObjectPtr<URTSHealthComponent>,double> LastHit;
 TMap<TWeakObjectPtr<ARTSUnit>,int32> Roles; // 0 iron, 1 wood, 2 food
 TMap<ERTSResource,int32> PrevStock;
 TMap<int32,double> HoldStart;
 TSet<int32> StallLogged;
 FDayReport Day;

 bool Finish(const FString& Error)
 {
  if(!Error.IsEmpty()) Test->AddError(Error);
  Test->TestTrue(TEXT("Campaign captures at least one eastern mine"),(LastMask&7)!=0);
  Test->TestTrue(TEXT("Campaign upgrades the sword line to level six or better"),SwordLevel>=6);
  Test->TestTrue(TEXT("TownHall survives the ten days"),Hall.IsValid() && Hall->IsAlive());
  Test->TestTrue(TEXT("Ten days contain real hostile interaction"),InteractiveDays>=6);
  Test->TestTrue(TEXT("Most days show a real defense or counterattack"),InteractiveDays>=6);
  Report+=FString::Printf(TEXT("TEN_DAY_END completedDays=%d minesCaptured=%d regionMask=%d swordLevel=%d hallAlive=%d hallDamaged=%d\n%s\n"),
   LoggedDays,MinesTotal,LastMask,SwordLevel,Hall.IsValid()?1:0,HallDamageSeen?1:0,*Error);
  FFileHelper::SaveStringToFile(Report,*(Directory/TEXT("metrics.log")));
  return true;
 }

 void Hit(FDayReport& ReportRef, AActor* Victim, URTSHealthComponent* Health, bool bFriendly)
 {
  const double Time=Health->GetLastDamageTime();
  const TWeakObjectPtr<URTSHealthComponent> Key(Health);
  const double* Previous=LastHit.Find(Key);
  if(Time>=0 && (!Previous || Time>*Previous))
  {
   if(bFriendly)++ReportRef.FriendlyHits;else ++ReportRef.EnemyHits;
   if(Victim && Victim==Hall.Get()){++HallHits;HallDamageSeen=true;}
  }
  LastHit.Add(Key,Time);
 }

 void Observe(UWorld* World, ARTSCampaign* Campaign)
 {
  if(!Hall.IsValid()||!Hall->IsAlive())
  {
   Hall=nullptr;
   for(TActorIterator<ARTSBuilding> It(World);It;++It)
    if(It->GetTeam()==0 && It->IsAlive() && It->GetBuildingId()==TEXT("TownHall")){Hall=*It;break;}
  }
  if(Hall.IsValid() && Hall->GetHealth())
  {const float Fraction=Hall->GetHealth()->GetHealthPercent();Day.HallMin=FMath::Min(Day.HallMin,Fraction);}
  int32 Near=0,Working=0;
  for(TActorIterator<ARTSUnit> It(World);It;++It)
  {
   if(!It->IsAlive() || !It->GetUnitData())continue;
   if(It->GetTeam()!=0)
   {
    if(It->GetUnitData()->bWorker)continue;
    const float Distance=FVector::Dist2D(It->GetActorLocation(),ARTSCampaign::HomeLocation());
    Day.MinEnemyDistance=FMath::Min(Day.MinEnemyDistance,Distance);
    if(Distance<=2600.0){++Near;Day.NearEnemies.Add(*It);}
   }
   else
   {
    if(It->GetGatherer() && It->GetGatherer()->IsWorking())++Working;
   }
   if(It->GetHealth())Hit(Day,*It,It->GetHealth(),It->GetTeam()==0);
  }
  for(TActorIterator<ARTSBuilding> It(World);It;++It)
   if(It->IsAlive() && It->GetHealth())Hit(Day,*It,It->GetHealth(),It->GetTeam()==0);
  Day.PeakNear=FMath::Max(Day.PeakNear,Near);Day.PeakWorking=FMath::Max(Day.PeakWorking,Working);
  auto* Stock=URTSBalanceSubsystem::Get(World)->GetResources(0);
  for(int32 I=0;I<8;++I)
  {
   const auto Resource=static_cast<ERTSResource>(I);const int32 Amount=Stock->GetAmount(Resource);
   if(const int32* Old=PrevStock.Find(Resource))Day.Delta[I]+=FMath::Max(0,Amount-*Old);
   PrevStock.Add(Resource,Amount);
  }
  const int32 Mask=Campaign->GetRegionMask();
  if(Mask!=LastMask)
  {
   for(int32 R=0;R<3;++R)if((Mask&(1<<R))&&!(LastMask&(1<<R)))
   {
    ++MinesTotal;HoldStart.Remove(R);
    UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_TEN_DAY_MINE_CAPTURED mine=%d elapsed=%.1f mask=%d"),R,Campaign->GetElapsed(),Mask);
   }
   LastMask=Mask;
  }
 }

 bool Place(UWorld* World, ARTSCampaign* Campaign, FName Kind)
 {
  if(auto* Existing=Campaign->FindServiceBuilding(Kind))return Existing->IsComplete();
  for(TActorIterator<ARTSBuilding> It(World);It;++It)
   if(It->GetTeam()==0 && It->IsAlive() && It->GetBuildingId()==Kind)
   {
    bool HasBuilder=false;ARTSUnit* Replacement=nullptr;double Distance=MAX_dbl;
    for(TActorIterator<ARTSUnit> U(World);U;++U)if(U->GetTeam()==0 && U->IsAlive() && U->GetUnitData()->bWorker)
    {
     const auto& O=U->GetCurrentOrder();
     if(O.Type==ERTSOrderType::Build && O.TargetActor.Get()==*It)HasBuilder=true;
     if(O.Type!=ERTSOrderType::Build && O.Type!=ERTSOrderType::ReturnResources)
     {const double D=FVector::DistSquared2D(U->GetActorLocation(),It->GetActorLocation());if(D<Distance){Distance=D;Replacement=*U;}}
    }
    if(!HasBuilder && Replacement)URTSOrderLibrary::IssueWorkOrder({Replacement},*It);
    return false; // keep real progress; replace a fallen builder using the ordinary work order
   }
  auto* PC=Cast<ARTSHumanController>(World->GetFirstPlayerController());if(!PC)return false;
  ARTSUnit* Worker=nullptr;
  for(TActorIterator<ARTSUnit> It(World);It;++It)
   if(It->GetTeam()==0 && It->IsAlive() && It->GetUnitData()->bWorker && It->GetCurrentOrder().Type!=ERTSOrderType::Build){Worker=*It;break;}
  if(!Worker)return false;
  PC->GetSelection()->SelectSingle(Worker,false);
  for(int32 Ring=1;Ring<=3;++Ring)for(int32 I=0;I<8;++I)
  {
   const float Angle=I*PI/4;const float Radius=650+Ring*350;
   const FVector Point=RTSCampaignTerrain::Ground(ARTSCampaign::HomeLocation()+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0));
   PC->BeginBuildingPlacement(Kind);
   if(PC->ConfirmBuildingPlacement(Point))return false; // paid placement; wait for real construction
  }
  PC->CancelBuildingPlacement();
  return false;
 }

 ARTSResourceNode* NearestNode(UWorld* World, ERTSResource Type)
 {
  ARTSResourceNode* Best=nullptr;double Distance=MAX_dbl;
  for(TActorIterator<ARTSResourceNode> It(World);It;++It)
  {
   if(It->GetResourceType()!=Type || It->GetRemaining()<=0 || !It->CanGather(0))continue;
   // Workers must not be fed to a standing guard camp: prefer the camp-free nearest node.
   bool Guarded=false;
   for(TActorIterator<ARTSUnit> Enemy(World);Enemy;++Enemy)
    if(Enemy->IsAlive() && Enemy->GetTeam()!=0 && Enemy->GetUnitData() && !Enemy->GetUnitData()->bWorker &&
       FVector::DistSquared2D(Enemy->GetActorLocation(),It->GetActorLocation())<FMath::Square(1600.f)){Guarded=true;break;}
   const double D=FVector::DistSquared2D(It->GetActorLocation(),ARTSCampaign::HomeLocation())+(Guarded?FMath::Square(6000.f):0.0);
   if(D<Distance){Distance=D;Best=*It;}
  }
  return Best;
 }

 void Gather(UWorld* World, bool bGuideStep)
 {
  int32 RoleCount[3]={0,0,0};
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->GetTeam()==0 && It->IsAlive() && It->GetUnitData()->bWorker)
   if(const int32* Role=Roles.Find(TWeakObjectPtr<ARTSUnit>(*It));Role && *Role>=0 && *Role<3)++RoleCount[*Role];
  for(TActorIterator<ARTSUnit> It(World);It;++It)
  {
   if(It->GetTeam()!=0 || !It->IsAlive() || !It->GetUnitData()->bWorker)continue;
   auto* Gatherer=It->GetGatherer();if(!Gatherer)continue;
   const TWeakObjectPtr<ARTSUnit> Key(*It);
   int32* Stored=Roles.Find(Key);
   if(!Stored)
   {
    int32 Role=0;for(int32 I=1;I<3;++I)if(RoleCount[I]<RoleCount[Role])Role=I;
    Roles.Add(Key,Role);++RoleCount[Role];Stored=Roles.Find(Key);
   }
   const auto Order=It->GetCurrentOrder();
   if(Order.Type==ERTSOrderType::Build || Order.Type==ERTSOrderType::ReturnResources)continue;
   if(Order.Type==ERTSOrderType::Gather)
   {
    auto* Node=Cast<ARTSResourceNode>(Order.TargetActor.Get());
    if(Node && Node->GetRemaining()>0)continue;
   }
   const ERTSResource Wanted=*Stored==0?ERTSResource::IronOre:*Stored==1?ERTSResource::Wood:ERTSResource::Food;
   ARTSResourceNode* Node=NearestNode(World,Wanted);
   if(!Node && !bGuideStep)Node=NearestNode(World,ERTSResource::Wood);
   if(!Node)continue;
   FRTSOrder O;O.Type=ERTSOrderType::Gather;O.TargetActor=Node;It->ReceiveOrder(O);++Day.GatherOrders;
  }
 }

 void Economy(UWorld* World, ARTSCampaign* Campaign)
 {
  ARTSHumanController* PC=Cast<ARTSHumanController>(World->GetFirstPlayerController());if(!PC)return;
  int32 Workers=0;
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->GetTeam()==0 && It->IsAlive())
  {if(It->GetUnitData()->bWorker)++Workers;}
  const bool bResearch=Place(World,Campaign,TEXT("ResearchInstitute"));
  const bool bBarracks=Place(World,Campaign,TEXT("Barracks"));
  // Replace losses with paid villagers so the real economy can keep running.
  if(Workers<6 && Workers>0)
  {
   if(Campaign->SelectServiceBuilding(TEXT("TownHall")) && Campaign->Train(TEXT("Peasant")))++Day.Peasants;
  }
 if(bResearch && Campaign->SelectServiceBuilding(TEXT("ResearchInstitute")))
 {
  // A real player keeps buying upgrades while the stores allow it, and levels the crew
  // early so the level-6 remote drop-off makes distant nodes worth working.
  for(int32 Attempt=0;Attempt<2 && Day.Upgrades<2;++Attempt)
  {
   FName Kind=Campaign->GetLevel(TEXT("Peasant"))<6?FName("Peasant"):Campaign->GetLevel(TEXT("Swordman"))<12?FName("Swordman"):FName("Archer");
   if(Campaign->Upgrade(Kind))++Day.Upgrades;
   else break;
  }
 }
  // A player under siege fortifies before spending on more troops.
  const float HallNow=Hall.IsValid()&&Hall->GetHealth()?Hall->GetHealth()->GetHealthPercent():1.f;
  int32 IntrudersNear=0,BarracksCount=0,TowerCount=0;
  for(TActorIterator<ARTSUnit> It(World);It;++It)
   if(It->IsAlive() && It->GetTeam()==1 && It->GetUnitData() && !It->GetUnitData()->bWorker
    && FVector::DistSquared2D(It->GetActorLocation(),ARTSCampaign::HomeLocation())<FMath::Square(2500.f))++IntrudersNear;
  for(TActorIterator<ARTSBuilding> It(World);It;++It)
   if(It->IsAlive() && It->GetTeam()==0)
   {
    if(It->GetBuildingId()==TEXT("Barracks"))++BarracksCount;
    if(It->GetBuildingId()==TEXT("Tower"))++TowerCount;
   }
  if(IntrudersNear>=4 && BarracksCount<2)Place(World,Campaign,TEXT("Barracks"));
  if((HallNow<0.97f || IntrudersNear>=4) && TowerCount<2)Place(World,Campaign,TEXT("Tower"));
  if(bBarracks)
  {
   for(int32 Attempt=0;Attempt<4;++Attempt)
   {
    if(Day.Trained>=8)break;
    if(Campaign->SelectServiceBuilding(TEXT("Barracks")) && Campaign->Train(TEXT("Swordman")))++Day.Trained;
    else break;
   }
  }
 }

 int32 NextMine(ARTSCampaign* Campaign) const
 {
  const int32 Mask=Campaign->GetRegionMask();int32 Best=-1;double Distance=MAX_dbl;
  for(int32 R=0;R<3;++R)if(!(Mask&(1<<R)))
  {
   const double D=FVector::DistSquared2D(RTSCampaignFactions::RegionCenters[R],ARTSCampaign::HomeLocation());
   if(D<Distance){Distance=D;Best=R;}
  }
  return Best;
 }

 void MoveTo(ARTSUnit* Unit, const FVector& Goal)
 {
  const auto& Current=Unit->GetCurrentOrder();
  if((Current.Type==ERTSOrderType::Move || Current.Type==ERTSOrderType::AttackMove) && FVector::Dist2D(Current.TargetLocation,Goal)<600)return;
  FRTSOrder O;O.Type=ERTSOrderType::Move;O.TargetLocation=Goal;Unit->ReceiveOrder(O);
 }

 void Attack(ARTSUnit* Unit, AActor* Target, bool bDefense)
 {
  const auto& Current=Unit->GetCurrentOrder();
  if(Current.Type==ERTSOrderType::Attack && Current.TargetActor.Get()==Target)return;
  FRTSOrder O;O.Type=ERTSOrderType::Attack;O.TargetActor=Target;Unit->ReceiveOrder(O);
  if(bDefense)++Day.DefenseOrders;else ++Day.CounterOrders;
 }

 void Army(UWorld* World, ARTSCampaign* Campaign)
 {
  TArray<ARTSUnit*> Combat;
  for(TActorIterator<ARTSUnit> It(World);It;++It)
   if(It->GetTeam()==0 && It->IsAlive() && It->GetUnitData() && !It->GetUnitData()->bWorker && It->GetUnitData()->AttackDamage>0)Combat.Add(*It);
  if(!Combat.Num())return;
  const FVector Home=ARTSCampaign::HomeLocation();
  TArray<ARTSUnit*> Intruders;
  for(TActorIterator<ARTSUnit> It(World);It;++It)
   if(It->GetTeam()!=0 && It->IsAlive() && It->GetUnitData() && !It->GetUnitData()->bWorker &&
      FVector::DistSquared2D(It->GetActorLocation(),Home)<FMath::Square(GuardRadius))Intruders.Add(*It);
  // A small home guard stays near the hall; the rest is free for mine pushes and sorties.
  const int32 GuardCount=FMath::Clamp(Combat.Num()/3,2,4);
  TArray<ARTSUnit*> Guard;double GuardDistance=MAX_dbl;
  ARTSUnit* Nearest=nullptr;
  for(int32 I=0;I<Combat.Num() && Guard.Num()<GuardCount;++I)
  {
   Nearest=nullptr;GuardDistance=MAX_dbl;
   for(ARTSUnit* U:Combat)
   {
    if(Guard.Contains(U))continue;
    const double D=FVector::DistSquared2D(U->GetActorLocation(),Home);
    if(D<GuardDistance){GuardDistance=D;Nearest=U;}
   }
   if(Nearest)Guard.Add(Nearest);
  }
  const int32 Mine=NextMine(Campaign);
  FVector Center=FVector::ZeroVector;TArray<ARTSUnit*> Guards;
  if(Mine>=0)
  {
   Center=RTSCampaignFactions::RegionCenters[Mine];
   for(TActorIterator<ARTSUnit> It(World);It;++It)
    if(It->GetTeam()!=0 && It->IsAlive() && It->GetUnitData() && !It->GetUnitData()->bWorker &&
       FVector::DistSquared2D(It->GetActorLocation(),Center)<FMath::Square(MineClearRadius))Guards.Add(*It);
  }
  const FVector Post=Home+FVector(-900,900,0);
  for(ARTSUnit* Unit:Combat)
  {
   const bool bGuard=Guard.Contains(Unit);
   const double HomeDistance=FVector::Dist2D(Unit->GetActorLocation(),Home);
   if(Intruders.Num()>0)
   {
    if(bGuard || HomeDistance<RecallRadius)
    {
     ARTSUnit* Threat=nullptr;double Distance=MAX_dbl;
     for(ARTSUnit* Enemy:Intruders)
     {const double D=FVector::DistSquared2D(Unit->GetActorLocation(),Enemy->GetActorLocation());if(D<Distance){Distance=D;Threat=Enemy;}}
     if(Threat)Attack(Unit,Threat,true);
    }
    else MoveTo(Unit,Post); // fly home because the base really is under attack
    continue;
   }
   if(bGuard){MoveTo(Unit,Post);continue;}
   if(Mine>=0 && Guards.Num()>0)
   {
    ARTSUnit* Target=nullptr;double Distance=MAX_dbl;
    for(ARTSUnit* Enemy:Guards)
    {const double D=FVector::DistSquared2D(Unit->GetActorLocation(),Enemy->GetActorLocation());if(D<Distance){Distance=D;Target=Enemy;}}
    if(Target)Attack(Unit,Target,false);else MoveTo(Unit,Center);
    continue;
   }
   if(Mine>=0)
   {
    const FVector Ground=RTSCampaignTerrain::Ground(Center);
    if(FVector::Dist2D(Unit->GetActorLocation(),Ground)>MineHoldRadius)MoveTo(Unit,Ground);
    else if(Unit->GetCurrentOrder().Type!=ERTSOrderType::Stop&&Unit->GetCurrentOrder().Type!=ERTSOrderType::Attack)
    {FRTSOrder O;O.Type=ERTSOrderType::Stop;Unit->ReceiveOrder(O);}
    if(!HoldStart.Contains(Mine))HoldStart.Add(Mine,Campaign->GetElapsed());
    else if(Campaign->GetElapsed()-HoldStart.FindRef(Mine)>20 && !StallLogged.Contains(Mine))
    {
     StallLogged.Add(Mine);
     double NearestEnemy=MAX_dbl;
     for(TActorIterator<ARTSUnit> It(World);It;++It)
      if(It->GetTeam()!=0 && It->IsAlive() && It->GetUnitData() && !It->GetUnitData()->bWorker)
       NearestEnemy=FMath::Min(NearestEnemy,FVector::Dist2D(It->GetActorLocation(),Center));
     UE_LOG(LogTemp,Display,TEXT("TEN_DAY_CAPTURE_STALL mine=%d holdSeconds=%.1f nearestEnemyCm=%.0f note=RegionGuards may still be alive away from the flag"),Mine,Campaign->GetElapsed()-HoldStart.FindRef(Mine),NearestEnemy);
    }
    continue;
   }
   // Mines are secured and the army is massed: stop camping and attack the enemy halls.
   if(MinesTotal>=3)
   {
    int32 Army=0;
    for(TActorIterator<ARTSUnit> It(World);It;++It)
     if(It->GetTeam()==0 && It->IsAlive() && It->GetUnitData() && !It->GetUnitData()->bWorker)++Army;
    if(Army>=8)
    {
     ARTSBuilding* EnemyHall=nullptr;double HallDistance=MAX_dbl;
     for(TActorIterator<ARTSBuilding> It(World);It;++It)
      if(It->GetTeam()==1 && It->IsAlive() && It->GetBuildingId()==TEXT("TownHall") && Campaign->GetActorFaction(*It)>=0)
      {
       const double D=FVector::DistSquared2D(It->GetActorLocation(),Unit->GetActorLocation());
       if(D<HallDistance){HallDistance=D;EnemyHall=*It;}
      }
     if(EnemyHall){Attack(Unit,EnemyHall,false);continue;}
    }
   }
   // All mines taken: only sortie when an enemy is actually near, never pin the army inside the base.
   ARTSUnit* EnemyTarget=nullptr;double Distance=FMath::Square(9000.0);
   for(TActorIterator<ARTSUnit> It(World);It;++It)
    if(It->GetTeam()!=0 && It->IsAlive() && It->GetUnitData() && !It->GetUnitData()->bWorker)
    {
     const double D=FVector::DistSquared2D(It->GetActorLocation(),Home);
     if(D<Distance){Distance=D;EnemyTarget=*It;}
    }
   if(EnemyTarget)Attack(Unit,EnemyTarget,false);else MoveTo(Unit,Post);
  }
 }

 void LogDay(UWorld* World, ARTSCampaign* Campaign)
 {
  int32 Army=0,Workers=0,Enemies=0;
  for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->IsAlive())
  {if(It->GetTeam()!=0)++Enemies;else if(It->GetUnitData()->bWorker)++Workers;else ++Army;}
  SwordLevel=Campaign->GetLevel(TEXT("Swordman"));
  const float HallFraction=Hall.IsValid()&&Hall->GetHealth()?Hall->GetHealth()->GetHealthPercent():0.f;
  FString Stock,Gain;
  for(int32 I=0;I<8;++I){Stock+=FString::Printf(TEXT("%s%d"),I?TEXT(","):TEXT(""),PrevStock.FindRef(static_cast<ERTSResource>(I)));Gain+=FString::Printf(TEXT("%s%d"),I?TEXT(","):TEXT(""),Day.Delta[I]);}
  const FString Line=FString::Printf(TEXT("TEN_DAY_DAY day=%d elapsed=%.3f stock=[%s] gained=[%s] army=%d workers=%d enemies=%d nearEnemies=%d peakNear=%d minEnemyCm=%.0f townHallPct=%.2f townHallMinPct=%.2f swordLevel=%d archerLevel=%d upgrades=%d trained=%d peasants=%d gatherOrders=%d peakWorking=%d defenseOrders=%d counterOrders=%d enemyHits=%d friendlyHits=%d townHallHits=%d regionMask=%d minesCaptured=%d defeated=%d completed=%d"),
   LoggedDays+1,Campaign->GetElapsed(),*Stock,*Gain,Army,Workers,Enemies,Day.NearEnemies.Num(),Day.PeakNear,Day.MinEnemyDistance,HallFraction,Day.HallMin,SwordLevel,Campaign->GetLevel(TEXT("Archer")),Day.Upgrades,Day.Trained,Day.Peasants,Day.GatherOrders,Day.PeakWorking,Day.DefenseOrders,Day.CounterOrders,Day.EnemyHits,Day.FriendlyHits,HallHits,Campaign->GetRegionMask(),MinesTotal,Campaign->IsDefeated(),Campaign->IsCompleted());
  FString Infrastructure;
  for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->GetTeam()==0 && It->IsAlive())
   Infrastructure+=FString::Printf(TEXT(" %s:%.2f/q%d"),*It->GetBuildingId().ToString(),It->GetBuildProgress(),It->GetProducer()->GetQueue().Num());
  const FString Diagnostic=FString::Printf(TEXT("TEN_DAY_INFRA day=%d buildings=%s lastMessage=%s"),LoggedDays+1,*Infrastructure,*Campaign->GetMessage());
  UE_LOG(LogTemp,Display,TEXT("%s"),*Diagnostic);Report+=Diagnostic+TEXT("\n");
  UE_LOG(LogTemp,Display,TEXT("%s"),*Line);Report+=Line+TEXT("\n");
  Test->TestTrue(FString::Printf(TEXT("Day %d: real enemy contact or counterattack"),LoggedDays+1),Day.NearEnemies.Num()>0 || (Day.CounterOrders>0 && Day.EnemyHits>0));
  Test->TestTrue(FString::Printf(TEXT("Day %d: a paid upgrade happens"),LoggedDays+1),Day.Upgrades>0);
  Test->TestTrue(FString::Printf(TEXT("Day %d: the TownHall is still standing"),LoggedDays+1),Hall.IsValid() && Hall->IsAlive());
  if(Day.Interactive())++InteractiveDays;
  ++LoggedDays;
  Day=FDayReport();
  HoldStart.Reset();StallLogged.Reset();
  FFileHelper::SaveStringToFile(Report,*(Directory/TEXT("metrics.log")));
 }

public:
 FRun(FAutomationTestBase* In,const FString& InDirectory):Test(In),Directory(InDirectory){}
 bool Update() override
 {
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Campaign=World?ARTSCampaign::Get(World):nullptr;
  if(FPlatformTime::Seconds()-Started>1800)return Finish(TEXT("TEN_DAY_BLOCKER real-time watchdog 1800s without completing 3000 campaign seconds; no time skip applied"));
  if(!Campaign || !Campaign->IsReady())return false;
  if(!Initialized)
  {
   Initialized=true;GuideStarted=World->GetTimeSeconds();World->GetWorldSettings()->SetTimeDilation(8.f);
   LastMask=Campaign->GetRegionMask();
   Report=TEXT("TEN_DAY_BEGIN real PIE, dilation=8, 3000 campaign seconds, natural tutorial, no fixture/free resources/enemy orders; real mining push + home guard + sorties; resource order Wood,Food,Iron,Steel,Amber,Crystal,Gold,Mana\n");
   UE_LOG(LogTemp,Display,TEXT("%s"),*Report);
   for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->GetTeam()==0 && It->GetUnitData()->bWorker){GuideWorker=*It;break;}
   if(!GuideWorker.IsValid())return Finish(TEXT("TEN_DAY_BLOCKER no starting worker"));
  }
  if(!NavChecked)
  {
   auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation Point;
   if(!Nav || !Nav->ProjectPointToNavigation(ARTSCampaign::HomeLocation()+FVector(500,0,0),Point,FVector(150,150,250)))
    return Finish(TEXT("TEN_DAY_BLOCKER navigation not generated at home; the test never edits or bakes map assets"));
   NavChecked=true;
  }
  if(Campaign->GetGuideStep()<7)
  {
   if(World->GetTimeSeconds()-GuideStarted>2400)return Finish(FString::Printf(TEXT("TEN_DAY_BLOCKER tutorial step=%d message=%s"),Campaign->GetGuideStep(),*Campaign->GetMessage()));
   if(!GuideWorker.IsValid() || !GuideWorker->IsAlive())return Finish(TEXT("TEN_DAY_BLOCKER tutorial worker lost"));
   switch(Campaign->GetGuideStep())
   {
    case 0:Campaign->AdvanceGuide();break;
    case 1:GuideWorker->SetSelected(true);break;
    case 2:if(!MoveSent){FRTSOrder O;O.Type=ERTSOrderType::Move;O.TargetLocation=GuideWorker->GetActorLocation()+FVector(0,350,-82);GuideWorker->ReceiveOrder(O);MoveSent=true;}break;
    case 3:Gather(World,true);break;
    case 4:if(Place(World,Campaign,TEXT("ResearchInstitute"))){Campaign->SelectServiceBuilding(TEXT("ResearchInstitute"));Campaign->Upgrade(TEXT("Swordman"));}break;
    case 5:if(Place(World,Campaign,TEXT("Barracks"))){Campaign->SelectServiceBuilding(TEXT("Barracks"));Campaign->Train(TEXT("Swordman"));}break;
    case 6:Campaign->NotifyPageOpened(0);Campaign->AdvanceGuide();break;
   }
   return false;
  }
  if(World->GetTimeSeconds()!=LastSample){Observe(World,Campaign);LastSample=World->GetTimeSeconds();}
  if(Campaign->GetElapsed()>=(LoggedDays+1)*ARTSCampaign::DaySeconds && LoggedDays<10)LogDay(World,Campaign);
  if(LoggedDays>=10){Test->TestTrue(TEXT("3000 campaign seconds elapse without time skipping"),Campaign->GetElapsed()>=3000);return Finish(TEXT(""));}
  if(Campaign->GetElapsed()-LastPolicy>=5){LastPolicy=Campaign->GetElapsed();Gather(World,false);Economy(World,Campaign);Army(World,Campaign);}
  return false;
 }
};
class FCleanup : public IAutomationLatentCommand
{
 FString PreviousBase;int32 PreviousActive;
public:
 FCleanup(const FString& Base,int32 Active):PreviousBase(Base),PreviousActive(Active){}
 bool Update() override
 {ARTSCampaign::SetSaveSlotOverride(*PreviousBase);ARTSCampaign::SetActiveSlot(PreviousActive);return true;}
};
}
bool FRTSCampaignTenDayTest::RunTest(const FString&)
{
 if(GEditor && GEditor->PlayWorld){AddError(TEXT("TEN_DAY_BLOCKER close PIE before running the isolated ten day test"));return false;}
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("TenDay"));
 const FString Directory=FPaths::ProjectSavedDir()/TEXT("Automation/TenDay")/Scope->Slot;
 IFileManager::Get().MakeDirectory(*Directory,true);
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(RTSTenDay::FRun(this,Directory));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));
 return true;
}
#endif
