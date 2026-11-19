#include "Campaign/RTSCampaign.h"
#include "Combat/RTSAcquisitionComponent.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Economy/RTSResourceNode.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
namespace
{
 int32 SiteCapacity(const FRTSCampaignDailySiteSave& S){return FMath::RoundToInt(900*S.Tier*S.Tier*(1+FMath::Min(S.Day,20)*.1f));}
 float SiteRecovery(const FRTSCampaignDailySiteSave& S){return 3*S.Tier+FMath::Min(S.Day,20)*.4f;}
 ERTSResource SiteType(int32 Day,int32 Slot)
 {
 // Rotate essentials so no material can become permanently unobtainable: refined daily, crystal/mana and
 // gold/amber alternate, and points persist once generated.
 if(Slot==0)return Day%2?ERTSResource::Food:ERTSResource::Wood;
 if(Slot==1)return Day>=1?ERTSResource::RefinedIron:ERTSResource::IronOre;
 if(Slot==2)return Day>=4?(Day%2?ERTSResource::Mana:ERTSResource::Crystal):Day>=2?ERTSResource::Crystal:ERTSResource::IronOre;
 return Day>=3?(Day%2?ERTSResource::Gold:ERTSResource::Amber):Day>=1?ERTSResource::Amber:ERTSResource::Food;
 }
}

ERTSResource ARTSCampaign::DailySiteTypeForDay(int32 Day,int32 Slot){return SiteType(FMath::Max(0,Day),FMath::Clamp(Slot,0,3));}
bool ARTSCampaign::SpawnDailySite(const FRTSCampaignDailySiteSave& State,bool bRestore)
{
 auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation P;
 P.Location=State.Position;
 if(!bRestore && (!Nav || !Nav->ProjectPointToNavigation(State.Position,P,FVector(80,80,150)) || FVector::Dist2D(P.Location,State.Position)>50 || FMath::Abs(P.Location.Z-State.Position.Z)>60))return false;
 FRTSCampaignDailySite Site;Site.State=State;
 if(!bRestore && State.Owner==1)
 {
  for(int32 I=0;I<3;++I)
  {
   const bool Bow=I==2;
   auto* U=Spawn(EnemyKindForDay(State.Day,Bow?I:I,Bow),1,P.Location+FVector(450+(I%2)*180,(I/2)*220-150,0));
   if(!U){for(auto Weak:Site.Guards)if(Weak.IsValid())Weak->Destroy();return false;}
   ApplyEnemyProgression(U,State.Day,State.Tier);
   U->SetCombatStance(ERTSCombatStance::Guard);Site.Guards.Add(U);ApplyFaction(U,(State.Day+State.Id)%4);EncounterIds.Add(U,10000+State.Id);
  }
 }
 const FTransform Transform(P.Location+FVector(0,0,90));
 auto* N=GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!N){for(auto Weak:Site.Guards)if(Weak.IsValid())Weak->Destroy();return false;}
 if(bStagingRestore){N->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));N->SetActorEnableCollision(false);N->SetActorHiddenInGame(true);}
 N->Configure(State.Type,SiteCapacity(State));N->ConfigureEconomy(1,SiteRecovery(State),SiteCapacity(State),4+State.Tier*2);N->ConfigureResourceTier(State.Tier);N->SetOwningTeam(State.Owner);N->SetRenewable(true);N->FinishSpawning(Transform);N->RestoreEconomyState(State.Remaining,State.RecoveryRemainder);
 Site.Node=N;Owned.Add(N);DailySites.Add(MoveTemp(Site));ClearSceneryNear(P.Location,1600);UE_LOG(LogTemp,Display,TEXT("DailySite id=%d day=%d owner=%d restore=%d at=%s"),State.Id,State.Day,State.Owner,bRestore,*P.Location.ToString());return true;
}
void ARTSCampaign::UpdateDaily(float Seconds)
{
 const int32 Day=GetDay();
 if(RaidDay!=Day)
 {
  RaidDay=Day;RaidMask=0;RaidWaves.Init(0,4);
  int32 Halls[4]={0,0,0,0};
  for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->IsAlive() && It->GetTeam()==1 && It->GetBuildingId()==TEXT("TownHall"))
  {const int32 F=GetActorFaction(*It);if(F>=0 && F<4)++Halls[F];}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_DAY_STATE day=%d halls=%d,%d,%d,%d dailySites=%d regionMask=%d"),Day+1,Halls[0],Halls[1],Halls[2],Halls[3],DailySites.Num(),RegionMask);
 }
 for(ARTSResourceNode* N:EnemyNodes)if(IsValid(N))N->UpdateCampaignCapture(Seconds);
 // Every dawn resets the enemy push budget per point, so yesterday's schedule never rolls straight on.
 if(DailyGeneratedDay!=Day)for(auto& Site:DailySites)if(Site.State.Owner==1){Site.State.Pulses=0;Site.State.NextPush=Day*DaySeconds+90;}
 // Managed economies buy upgrades; dawn never changes their stats.
 if(DailyGeneratedDay!=Day && DailySites.Num()>=32)
 {
  for(auto& Site:DailySites){Site.State.Day=Day;Site.State.Tier=FMath::Clamp(1+Day/2,1,3);if(auto* Node=Site.Node.Get()){Node->ConfigureEconomy(1,SiteRecovery(Site.State),SiteCapacity(Site.State),4+Site.State.Tier*2);Node->ConfigureResourceTier(Site.State.Tier);}}
  DailyGeneratedDay=Day;Message=TEXT("新一天：产区强化，未清理敌占点重新准备推进；已控制产区保留。");
 }
 if(DailyGeneratedDay!=Day && DailySites.Num()<32 && Elapsed-LastDailyGenerationAttempt>=10)
 {
  LastDailyGenerationAttempt=Elapsed;
  auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
  int32 ProjectedCount=0,OverlapCount=0,PathFailCount=0,PathCount=0,SpawnCount=0;
  // Buildings carve navigation. Never use one hard-coded point that can be built over.
  // Try living units and several projected approaches around home; retain complete-path validation.
  TArray<FVector> PathOrigins;
  auto AddOrigin=[&](const FVector& Point)
  {
   if(PathOrigins.Num()>=10)return;
   FNavLocation Projected;
   if(!Nav || !Nav->ProjectPointToNavigation(Point,Projected,FVector(240,240,500)))return;
   for(const auto& Existing:PathOrigins)if(FVector::DistSquared2D(Existing,Projected.Location)<FMath::Square(200.))return;
   PathOrigins.Add(Projected.Location);
  };
  for(TActorIterator<ARTSUnit> It(GetWorld());It && PathOrigins.Num()<2;++It)
   if(It->IsAlive() && It->GetTeam()==0 && FVector::Dist2D(It->GetActorLocation(),HomeLocation())<4500)AddOrigin(It->GetActorLocation());
  for(double Radius:{3200.,1800.,900.})for(int32 I=0;I<8;++I)
  {
   const double Angle=I*UE_PI/4.;
   AddOrigin(RTSCampaignTerrain::Ground(HomeLocation()+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0)));
  }
  for(int32 Slot=0;Slot<4 && DailySites.Num()<32;++Slot)
  {
   const int32 Id=Day*4+Slot;if(Id>=10000)break;
   bool Exists=false;for(const auto& Site:DailySites)Exists|=Site.State.Id==Id;if(Exists)continue;
   for(int32 Try=0;Try<160;++Try)
   {
    // Coprime cell traversal covers the entire 8x10 compact battlefield, not a ring around home.
    // Each daily slot starts in another quadrant; deterministic jitter avoids repeated straight rows.
    const int32 Cell=(Day*23+Slot*19+Try*37)%80;
    FRandomStream CandidateSeed(20261108+Day*997+Slot*131+Try*17);
    const double U=(Cell%10+.5+CandidateSeed.FRandRange(-.28f,.28f))/10.0;
    const double V=(Cell/10+.5+CandidateSeed.FRandRange(-.28f,.28f))/8.0;
    FVector Candidate=RTSCampaignMapSpace::MapToWorld(FVector2D(U,V));
    // One daily material point is discoverable near the base outskirts, not all in far corners.
    // Rotate its material slot each day; later attempts still fall back to the full battlefield.
    if(Slot==Day%4 && Try<80)
    {
     const double Angle=CandidateSeed.FRandRange(0.f,2.f*UE_PI);
     const double Radius=CandidateSeed.FRandRange(5200.f,7600.f);
     Candidate=HomeLocation()+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);
    }
    FVector Position=RTSCampaignTerrain::Ground(Candidate);FNavLocation Projected;
    if(!Nav || !Nav->ProjectPointToNavigation(Position,Projected,FVector(100,100,200)) || FVector::Dist2D(Position,Projected.Location)>80 || FMath::Abs(Projected.Location.Z-Position.Z)>65)continue;
    ++ProjectedCount;
    Position=Projected.Location;bool Overlap=FVector::Dist2D(Position,HomeLocation())<5000;
    for(const FVector& Center:RTSCampaignFactions::RegionCenters)Overlap|=FVector::Dist2D(Center,Position)<1800;
    for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)Overlap|=FVector::Dist2D(It->GetActorLocation(),Position)<(It->GetTeam()==0?4500:1100);
    for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->IsAlive() && It->GetTeam()==0)Overlap|=FVector::Dist2D(It->GetActorLocation(),Position)<3500;
    for(TActorIterator<ARTSResourceNode> It(GetWorld());It;++It)Overlap|=FVector::Dist2D(It->GetActorLocation(),Position)<1100;
    if(Overlap){++OverlapCount;continue;}
    bool bReachable=false;
    for(int32 Origin=0;Origin<PathOrigins.Num();++Origin)
    {
     auto* Path=Nav->FindPathToLocationSynchronously(GetWorld(),PathOrigins[Origin],Position);
     if(Path && Path->IsValid() && !Path->IsPartial())
     {
      if(Origin>0)Swap(PathOrigins[0],PathOrigins[Origin]);
      bReachable=true;break;
     }
    }
    if(!bReachable){++PathFailCount;continue;}
    ++PathCount;
    int32 Living=0;for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)Living+=It->IsAlive();
    FRTSCampaignDailySiteSave S;S.Id=Id;S.Day=Day;S.Tier=FMath::Clamp(1+Day/2,1,3);S.Type=SiteType(Day,Slot);S.Position=Position;S.Owner=Slot<3 && Living<=197?1:RTSTeam::Neutral;S.Remaining=SiteCapacity(S);S.NextPush=Day*DaySeconds+90;
    if(SpawnDailySite(S)){++SpawnCount;break;}
   }
  }
  int32 Today=0;for(const auto& Site:DailySites)Today+=Site.State.Id/4==Day;
  if(Today==4)
  {
   DailyGeneratedDay=Day;Message=FString::Printf(TEXT("第%d日：4处新资源点，基地外围与全图均有分布；部分由敌军先占。工人可冒险采集，战斗兵驻守5秒夺点。首日只驻守不推进。"),Day+1);
   UE_LOG(LogTemp,Display,TEXT("DailyGenerated day=%d today=%d total=%d origins=%d"),Day+1,Today,DailySites.Num(),PathOrigins.Num());
  }
  if(Today<4 && Elapsed-LastDailyDiagnostic>30){UE_LOG(LogTemp,Warning,TEXT("DailyCandidates day=%d today=%d projected=%d overlap=%d origins=%d pathFail=%d path=%d spawned=%d"),Day,Today,ProjectedCount,OverlapCount,PathOrigins.Num(),PathFailCount,PathCount,SpawnCount);LastDailyDiagnostic=Elapsed;}
 }
 for(auto& Site:DailySites)
 {
  auto* N=Site.Node.Get();if(!N)continue;
  Site.Guards.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& U){return !U.IsValid() || !U->IsAlive();});
  const uint8 NodeTeam=N->GetOwningTeam();
  if(Site.State.Owner!=NodeTeam){Site.State.Owner=NodeTeam;Site.State.Capture=0;if(NodeTeam==1){Site.State.Pulses=0;Site.State.Day=Day;Site.State.NextPush=Elapsed+90;}}
 }
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  auto* U=*It;const int32* Id=EncounterIds.Find(U);if(!Id || *Id<20000 || *Id>=30000 || !U->IsAlive())continue;
  if(const auto* Acquisition=U->FindComponentByClass<URTSAcquisitionComponent>();Acquisition && Acquisition->IsSupportingAlly())continue;
  AActor* Target=nullptr;double Best=FMath::Square(1100.);
  for(TActorIterator<ARTSUnit> P(GetWorld());P;++P)if(P->IsAlive() && P->GetTeam()==0){const double D=FVector::DistSquared2D(U->GetActorLocation(),P->GetActorLocation());if(D<Best && U->GetAttack()->HasLineOfSight(*P)){Best=D;Target=*P;}}
  for(TActorIterator<ARTSBuilding> B(GetWorld());B;++B)if(B->IsAlive() && B->GetTeam()==0){const double D=FVector::DistSquared2D(U->GetActorLocation(),B->GetActorLocation());if(D<Best && U->GetAttack()->HasLineOfSight(*B)){Best=D;Target=*B;}}
  if(Target && !U->GetAttack()->IsSwinging() && U->GetCurrentOrder().TargetActor.Get()!=Target)URTSOrderLibrary::IssueAttackOrder({U},Target);
  else if(!Target)
  {
   ARTSResourceNode* Front=nullptr;double Nearest=FMath::Square(3000.);
   for(const auto& Site:DailySites)if(auto* Node=Site.Node.Get())if(Node->GetOwningTeam()!=1){const double D=FVector::DistSquared2D(U->GetActorLocation(),Node->GetActorLocation());if(D<Nearest){Nearest=D;Front=Node;}}
   const FVector Destination=Front?Front->GetActorLocation()+FVector(350,0,-90):HomeLocation()+FVector(1100,0,10);
   if(Front && FVector::Dist2D(U->GetActorLocation(),Front->GetActorLocation())<650){if(U->GetCurrentOrder().Type!=ERTSOrderType::Stop){FRTSOrder O;O.Type=ERTSOrderType::Stop;U->ReceiveOrder(O);}}
   else if(U->GetCurrentOrder().Type!=ERTSOrderType::Move || FVector::Dist2D(U->GetCurrentOrder().TargetLocation,Destination)>100){FRTSOrder O;O.Type=ERTSOrderType::Move;O.TargetLocation=Destination;U->ReceiveOrder(O);}
  }
 }
}
void ARTSCampaign::SnapshotDailySites(TArray<FRTSCampaignDailySiteSave>& Out) const
{
 Out.Reset();for(const auto& Site:DailySites){auto S=Site.State;if(auto* N=Site.Node.Get()){S.Remaining=N->GetRemaining();S.RecoveryRemainder=N->GetRecoveryRemainder();S.Owner=N->GetOwningTeam();}Out.Add(S);}
}
int32 ARTSCampaign::RestoreDailySites(const TArray<FRTSCampaignDailySiteSave>& States){DailySites.Reset();int32 Failed=0;for(const auto& S:States)if(!SpawnDailySite(S,true))++Failed;return Failed;}
void ARTSCampaign::RegisterDailyUnit(ARTSUnit* U,int32 Encounter){if(Encounter<20000)for(auto& Site:DailySites)if(Site.State.Id==Encounter-10000)Site.Guards.Add(U);}
FString ARTSCampaign::GetDailyOverview() const
{
 int32 Enemy=0,Player=0;for(const auto& S:DailySites){Enemy+=S.State.Owner==1;Player+=S.State.Owner==0;}
 return FString::Printf(TEXT("每日前线：我方%d / 敌占%d / 共%d\n资源点驻军守卫；增援由敌方基地采集、付费训练。战斗兵驻守5秒夺取。"),Player,Enemy,DailySites.Num());
}
