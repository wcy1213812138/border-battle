#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignEconomyController.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Campaign/RTSCampaignBuildingGround.h"
#include "Campaign/RTSCampaignEnemyProgression.h"
#include "Campaign/RTSCampaignEnemyResourcePlacement.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSGathererComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Combat/RTSHealthComponent.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"
#include "Combat/RTSAttackComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Engine/World.h"
#include "Combat/RTSAcquisitionComponent.h"
int32 ARTSCampaign::GetActorFaction(const AActor* Actor) const
{
 if(!Actor)return -1;
 if(const int32* F=EconomicFactions.Find(const_cast<AActor*>(Actor)))return *F;
 return -1;
}
URTSResourceComponent* ARTSCampaign::GetActorAccount(const AActor* Actor) const
{
 const int32 F=GetActorFaction(Actor);
 if(F>=0)return FactionAccounts.IsValidIndex(F)?FactionAccounts[F].Get():nullptr;
 const auto* U=Cast<ARTSUnit>(Actor);const auto* B=Cast<ARTSBuilding>(Actor);
 auto* Balance=URTSBalanceSubsystem::Get(this);
 return Balance && ((U && U->GetTeam()==0) || (B && B->GetTeam()==0))?Balance->GetResources(0):nullptr;
}
bool ARTSCampaign::IsManagedEconomyUnit(const ARTSUnit* U) const{return GetActorFaction(U)>=0;}
bool ARTSCampaign::CanFactionTrain(const AActor* Producer,FName Kind) const
{
 const int32 F=GetActorFaction(Producer);const auto* B=Cast<ARTSBuilding>(Producer);
 return bReady && !bTransaction && !bStagingRestore && GuideStep>=7 && B && B->IsAlive() && B->IsComplete() &&
  EconomyStates.IsValidIndex(F) && EconomyStates[F].Active && RTSCampaignProgression::Service(Kind)==B->GetBuildingId() &&
  RTSCampaignEnemyProgression::IsUnlocked(EconomyStates[F].Levels,Kind,GetDay());
}
void ARTSCampaign::RegisterProducedUnit(ARTSUnit* U,const ARTSBuilding* B)
{
 const int32 F=GetActorFaction(B);if(!U || F<0)return;
 EconomicFactions.Add(U,F);ApplyFaction(U,F);EncounterIds.Add(U,30000+F);
 const int32 L=FMath::Max(1,EconomyStates[F].Levels.FindRef(U->GetUnitData()->BalanceId));
 ApplyEnemyProgression(U,L-1);U->SetCombatStance(ERTSCombatStance::Guard);
}
bool ARTSCampaign::RestoreEnemyNode(const FRTSEnemyNodeSave& S)
{
 FTransform T(S.Feet+FVector(0,0,90));
 auto* N=GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!N)return false;
 if(bStagingRestore){N->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));N->SetActorHiddenInGame(true);N->SetActorEnableCollision(false);}
 N->Configure(S.Type,6000);N->ConfigureEconomy(1,0,6000,4);N->ConfigureResourceTier(1);N->SetOwningTeam(S.Owner);N->FinishSpawning(T);
 N->RestoreEconomyState(S.Remaining,S.Remainder);if(!N->RestoreOwnership(S.Capture)){N->Destroy();return false;}
 EnemyNodes.Add(N);EnemyNodeFactions.Add(S.Faction);Owned.Add(N);return true;
}
bool ARTSCampaign::BootstrapEnemyEconomy()
{
 // One explicit bootstrap, also used on the staged old-save generation. No offline income.
 EconomyStates.SetNum(4);FactionAccounts.SetNum(4);
 RTSCampaignEnemyResourcePlacement::FContext Sites(GetWorld());
 if(!Sites.Ready()){UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_ECONOMY_NODE_CONTEXT_REJECT terrain/nav unavailable"));return false;}
 // During legacy migration ignore only the old campaign generation. Current staged
 // sites still participate explicitly even though their collision is disabled.
 TSet<const AActor*> ReplacedActors;
 if(bStagingRestore)
 {
  for(TActorIterator<ARTSCampaign> It(GetWorld());It;++It)if(*It!=this)for(AActor* A:It->Owned)if(IsValid(A)){Sites.Query.AddIgnoredActor(A);ReplacedActors.Add(A);}
  for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->GetTeam()==0 && !It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged"))){Sites.Query.AddIgnoredActor(*It);ReplacedActors.Add(*It);}
 }
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->IsAlive() && !ReplacedActors.Contains(*It))Sites.AddBuilding(*It);
 for(TActorIterator<ARTSResourceNode> It(GetWorld());It;++It)if(!ReplacedActors.Contains(*It))Sites.AddResource(*It);
 for(int32 F=0;F<4;++F)
 {
  auto& S=EconomyStates[F];S.Active=!(RegionMask&(1<<RTSCampaignFactions::RegionIndex[F]));
  S.Stock={{ERTSResource::Wood,240},{ERTSResource::Food,300}};S.NextRaid=Elapsed+RaidPlan(GetDay(),F).Launch;
  auto* Account=NewObject<URTSResourceComponent>(this);FactionAccounts[F]=Account;Account->SetAmounts(S.Stock);
  if(!S.Active)continue;
  const FVector Base=RTSCampaignFactions::Base[F];
  ARTSBuilding* Hall=nullptr;
  for(int32 I=0;I<3;++I)
  {
   const FName K=I==0?FName("TownHall"):I==1?FName("Barracks"):FName("ArcheryRange");
   FVector Formula,Feet;
   if(!RTSCampaignBuildingGround::FormulaSite(F,K,Formula) || !RTSCampaignBuildingGround::Sample(GetWorld(),K,Formula,0,Feet))
   {UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_ECONOMY_GROUND_REJECT faction=%d kind=%s"),F,*K.ToString());return false;}
   // Both new games and old-save migration use actual map collision, never analytic Z alone.
   FTransform T(Feet);auto* B=GetWorld()->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if(!B)return false;if(bStagingRestore){B->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));B->SetActorHiddenInGame(true);B->SetActorEnableCollision(false);}B->Configure(K,1,true);B->FinishSpawning(T);Buildings.Add(B);Owned.Add(B);EconomicFactions.Add(B,F);
   Sites.AddBuilding(B);if(K==TEXT("TownHall"))Hall=B;
  }
  TArray<FVector> HallPoints;
  if(!Hall || !Sites.HallAccess(Hall,HallPoints)){UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_ECONOMY_HALL_ACCESS_REJECT faction=%d"),F);return false;}
  const uint32 NodeSeed=RTSCampaignEnemyResourcePlacement::Seed(CampaignId,F);FRandomStream Random(static_cast<int32>(NodeSeed));
  const float PatchX=Random.FRandRange(-650.f,650.f),PatchY=Random.FRandRange(-450.f,750.f);const FVector Patch(PatchX,PatchY,0);
  for(int32 I=0;I<static_cast<int32>(ERTSResource::Count);++I)
  {
   FRTSEnemyNodeSave N;N.Faction=F;N.Type=static_cast<ERTSResource>(I);
   if(!Sites.Select(Random,F,N.Type,Patch,HallPoints,N.Feet) || !RestoreEnemyNode(N))
   {UE_LOG(LogTemp,Error,TEXT("CAMPAIGN_ECONOMY_NODE_REJECT faction=%d type=%d seed=%u attempts=%d"),F,I,NodeSeed,RTSCampaignEnemyResourcePlacement::AttemptsPerType);return false;}
   Sites.AddResource(EnemyNodes.Last().Get());
   UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ECONOMY_NODE faction=%d type=%d seed=%u feet=%s finite=6000 slots=4"),F,I,NodeSeed,*N.Feet.ToString());
  }
  // Two initial workers; replacements are paid TownHall orders.
  for(int32 I=0;I<2;++I)if(auto* U=Spawn(TEXT("Peasant"),1,RTSCampaignTerrain::Ground(Base+FVector(-700-I*170,1100,0))))
  {EconomicFactions.Add(U,F);ApplyFaction(U,F);EncounterIds.Add(U,30000+F);ApplyEnemyProgression(U,0);}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ECONOMY_BOOTSTRAP faction=%d staged=%d nodes=%d stockWood=240 stockFood=300"),F,bStagingRestore,EnemyNodes.Num());
 }
 return true;
}
void ARTSCampaign::StartEconomyTrees()
{
 for(int32 F=0;F<4;++F){auto* AI=GetWorld()->SpawnActor<ARTSCampaignEconomyController>();if(AI){Owned.Add(AI);EconomyControllers.Add(AI);AI->Start(this,F);}}
}
void ARTSCampaign::StepFactionEconomy(int32 F)
{
 if(!bReady || bTransaction || bStagingRestore || GuideStep<7 || !EconomyStates.IsValidIndex(F))return;
 auto& S=EconomyStates[F];if(!S.Active)return;
 ARTSBuilding* Hall=nullptr;TArray<ARTSBuilding*> Producers;
 for(ARTSBuilding* B:Buildings)if(IsValid(B) && B->IsAlive() && GetActorFaction(B)==F){Producers.Add(B);if(B->GetBuildingId()==TEXT("TownHall"))Hall=B;}
 if(!Hall || (RegionMask&(1<<RTSCampaignFactions::RegionIndex[F])))
 {S.Active=false;for(auto* B:Producers)B->GetProducer()->SetComponentTickEnabled(false);return;}
 TArray<ARTSUnit*> Workers,Army;int32 Living=0,Queued=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->IsAlive()){++Living;if(GetActorFaction(*It)==F)(It->GetUnitData()->bWorker?Workers:Army).Add(*It);}
 for(ARTSBuilding* B:Buildings)if(IsValid(B) && B->IsAlive())Queued+=B->GetProducer()->GetQueue().Num();
 // Defend actual economic buildings before committing to a distant raid.
 TArray<ARTSUnit*> Intruders;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->IsAlive() && It->GetTeam()==0)
 {
  for(auto* B:Producers)if(FVector::DistSquared2D(B->GetActorLocation(),It->GetActorLocation())<FMath::Square(1600.f))
  {Intruders.Add(*It);break;}
 }
 bool bDefending=false;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  auto* Defender=*It;const auto* Data=Defender->GetUnitData();
  if(!Defender->IsAlive() || Defender->GetTeam()!=1 || !Data || Data->bWorker || Data->bHealer || Data->AttackDamage<=0 || GetUnitFaction(Defender)!=F)continue;
  if(const auto* Acquisition=Defender->FindComponentByClass<URTSAcquisitionComponent>();Acquisition && Acquisition->IsSupportingAlly())
  {if(FVector::DistSquared2D(Defender->GetActorLocation(),Hall->GetActorLocation())<=FMath::Square(3200.f))bDefending=true;continue;}
  const auto& Current=Defender->GetCurrentOrder();
  if(Current.Type==ERTSOrderType::Attack && Current.TargetActor.IsValid() && FVector::DistSquared2D(Current.TargetActor->GetActorLocation(),Hall->GetActorLocation())>FMath::Square(3200.f) && !S.Deployed)
  {URTSOrderLibrary::IssueStopOrder({Defender});continue;}
  if(FVector::DistSquared2D(Defender->GetActorLocation(),Hall->GetActorLocation())>FMath::Square(3200.f))continue;
  ARTSUnit* Threat=nullptr;double Best=FMath::Square(2400.f);
  for(auto* Invader:Intruders)
  {
   const double Distance=FVector::DistSquared2D(Defender->GetActorLocation(),Invader->GetActorLocation());
   if(Distance<Best && Defender->GetAttack()->HasLineOfSight(Invader)){Best=Distance;Threat=Invader;}
  }
  if(!Threat)continue;
  bDefending=true;
  if(!Defender->GetAttack()->IsSwinging() && (Defender->GetCurrentOrder().Type!=ERTSOrderType::Attack || Defender->GetCurrentOrder().TargetActor.Get()!=Threat))URTSOrderLibrary::IssueAttackOrder({Defender},Threat);
 }
 if(bDefending)for(auto* Worker:Workers)
 {
  const FVector Safe=Hall->GetActorLocation()+FVector(-700,0,0);
  if(FVector::DistSquared2D(Worker->GetActorLocation(),Safe)>FMath::Square(300.f) && Worker->GetCurrentOrder().Type!=ERTSOrderType::Move)
  {FRTSOrder O;O.Type=ERTSOrderType::Move;O.TargetLocation=Safe;Worker->ReceiveOrder(O);}
 }
 // Gather toward real upcoming bills, including paid worker replacements/expansion.
 // Two bootstrap workers sustain staples; additional trained workers bring materials.
 auto* Account=FactionAccounts[F].Get();if(!Account)return;
 TMap<ERTSResource,int32> Demand={{ERTSResource::Wood,800},{ERTSResource::Food,800}};
 auto Need=[&](const TMap<ERTSResource,int32>& Cost,int32 Copies=1)
 {for(const auto& P:Cost)Demand.FindOrAdd(P.Key)=FMath::Max(Demand.FindRef(P.Key),P.Value*Copies+((P.Key==ERTSResource::Wood || P.Key==ERTSResource::Food)?150:0));};
 TMap<FName,int32> Counts;int32 FactionQueued=0;
 for(auto* U:Army)++Counts.FindOrAdd(U->GetUnitData()->BalanceId);
 for(auto* B:Producers)for(FName K:B->GetProducer()->GetQueue()){++Counts.FindOrAdd(K);++FactionQueued;}
 const int32 WorkerTarget=RTSCampaignEnemyProgression::DesiredWorkers(GetDay());
 if(Workers.Num()+Counts.FindRef(TEXT("Peasant"))<WorkerTarget)Need(RTSCampaignProgression::TrainCost(TEXT("Peasant"),FMath::Max(1,S.Levels.FindRef(TEXT("Peasant")))),WorkerTarget-Workers.Num()-Counts.FindRef(TEXT("Peasant")));
 for(auto* B:Producers)if(B->IsComplete())
 {
  const FName Research=RTSCampaignEnemyProgression::NextResearch(S.Levels,B->GetBuildingId(),GetDay());
  if(!Research.IsNone())Need(RTSCampaignProgression::ResearchCost(Research));
  for(FName K:RTSCampaignEnemyProgression::Recruitment(S.Levels,B->GetBuildingId(),GetDay(),Counts))Need(RTSCampaignProgression::TrainCost(K,FMath::Max(1,S.Levels.FindRef(K))),3);
 }
 for(const auto& P:Counts)if(P.Key!=TEXT("Peasant"))
 {
  const int32 L=FMath::Max(1,S.Levels.FindRef(P.Key));
  if(L<RTSCampaignEnemyProgression::MaxNextLevel(GetDay()))Need(RTSCampaignProgression::UpgradeCost(P.Key,L+1));
 }
 bool bNeedMaterial=false;for(const auto& P:Demand)if(P.Key!=ERTSResource::Wood && P.Key!=ERTSResource::Food && Account->GetAmount(P.Key)<P.Value)bNeedMaterial=true;
 for(int32 I=0;I<Workers.Num();++I)
 {
  if(bDefending)continue;
  auto* U=Workers[I];const auto Type=U->GetCurrentOrder().Type;if(Type==ERTSOrderType::ReturnResources)continue;
  const bool bMaterialWorker=I>=2 && bNeedMaterial && Account->GetAmount(ERTSResource::Wood)>=200 && Account->GetAmount(ERTSResource::Food)>=200;
  if(Type==ERTSOrderType::Gather){auto* N=Cast<ARTSResourceNode>(U->GetCurrentOrder().TargetActor.Get());if(N && N->GetOwningTeam()==1 && N->GetRemaining()>0 && (U->GetGatherer()->GetCarriedAmount()>0 || (Account->GetAmount(N->GetResourceType())<Demand.FindRef(N->GetResourceType()) && !(bMaterialWorker && (N->GetResourceType()==ERTSResource::Wood || N->GetResourceType()==ERTSResource::Food)))))continue;}
  ARTSResourceNode* Best=nullptr;int32 Score=MAX_int32;
  for(int32 J=0;J<EnemyNodes.Num();++J)if(EnemyNodeFactions[J]==F && IsValid(EnemyNodes[J]) && EnemyNodes[J]->GetRemaining()>0 && EnemyNodes[J]->GetOwningTeam()==1)
  {
   const auto R=EnemyNodes[J]->GetResourceType();const int32 Wanted=Demand.FindRef(R),Amount=Account->GetAmount(R);if(Wanted<=Amount)continue;
   const int32 Bias=bMaterialWorker && R!=ERTSResource::Wood && R!=ERTSResource::Food?-3000:(R==(I%2?ERTSResource::Food:ERTSResource::Wood) && Amount<400)?-2000:0;
   const int32 Rank=Amount*1000/Wanted+Bias;if(Rank<Score){Score=Rank;Best=EnemyNodes[J];}
  }
  if(Best){FRTSOrder O;O.Type=ERTSOrderType::Gather;O.TargetActor=Best;U->ReceiveOrder(O);}
 }
 // Research is a real purchase and persists as the advanced kind's level-one entry.
 // No research building/support barracks or troops are granted by this policy.
 if(Workers.Num()>=2 && Army.Num()>=3)for(auto* B:Producers)if(B->IsComplete())
 {
  const FName K=RTSCampaignEnemyProgression::NextResearch(S.Levels,B->GetBuildingId(),GetDay());
  if(RTSCampaignEnemyProgression::BuyResearch(S.Levels,Account,K,B->GetBuildingId(),GetDay()))
   UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ECONOMY_RESEARCH faction=%d day=%d kind=%s paid=1"),F,GetDay()+1,*K.ToString());
 }
 // Upgrade a fielded kind, lowest level first, at most once per economic step.
 TArray<FName> UpgradeKinds;Counts.GetKeys(UpgradeKinds);
 UpgradeKinds.Remove(FName("Peasant"));
 UpgradeKinds.Sort([&](FName A,FName B){const int32 LA=FMath::Max(1,S.Levels.FindRef(A)),LB=FMath::Max(1,S.Levels.FindRef(B));return LA==LB?RTSCampaignProgression::Index(A)<RTSCampaignProgression::Index(B):LA<LB;});
 if(Army.Num()>=3)for(FName K:UpgradeKinds)if(RTSCampaignEnemyProgression::BuyUpgrade(S.Levels,Account,K,GetDay()))
 {
  const int32 L=S.Levels.FindRef(K);for(auto* U:Army)if(U->GetUnitData()->BalanceId==K)ApplyEnemyProgression(U,L-1);
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ECONOMY_UPGRADE faction=%d day=%d kind=%s level=%d paid=1"),F,GetDay()+1,*K.ToString(),L);break;
 }
 for(auto* B:Producers)
 {
  if(Living+Queued>=200 || !B->IsComplete() || !B->GetProducer()->GetQueue().IsEmpty())continue;
  const int32 ArmyCap=FMath::Min(18,6+GetDay()*2);
  TArray<FName> Candidates;
  if(B->GetBuildingId()==TEXT("TownHall")){if(Workers.Num()+Counts.FindRef(TEXT("Peasant"))<WorkerTarget)Candidates.Add(FName("Peasant"));}
  else if(Army.Num()+FactionQueued<ArmyCap)Candidates=RTSCampaignEnemyProgression::Recruitment(S.Levels,B->GetBuildingId(),GetDay(),Counts);
  for(FName K:Candidates){FString Error;if(B->GetProducer()->Enqueue(K,Error)){++Queued;++FactionQueued;++Counts.FindOrAdd(K);UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ECONOMY_TRAIN faction=%d kind=%s paid=1"),F,*K.ToString());break;}}
 }
 if(bDefending)return; // Local defence takes priority without resetting paid production or raid timers.
 // Rotating pressure: two factions share a raid day and the other two stay home to
 // rebuild, so the player keeps a real window to take mines and sortie out.
 const int32 Day=GetDay();
 const bool bRaidDay=((Day+F)&1)==0;
 const auto Plan=RaidPlan(Day,F);
 const FVector Home=HomeLocation();
 int32 SiegeCount=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
  if(It->IsAlive() && It->GetTeam()==1 && It->GetUnitData() && !It->GetUnitData()->bWorker && FVector::DistSquared2D(It->GetActorLocation(),Home)<FMath::Square(2600.f))++SiegeCount;
 if(!S.Deployed && bRaidDay && SiegeCount<12 && Elapsed>=S.NextRaid && Elapsed>=Day*DaySeconds+Plan.Launch && Army.Num()>=3)
 {
  S.Deployed=true;S.RetreatAt=Elapsed+105;NotifyFirstIncoming(Army[0]);
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RAID_DEPLOY faction=%d day=%d army=%d swordLevel=%d archerLevel=%d siege=%d wood=%d food=%d"),F,Day+1,Army.Num(),FMath::Max(1,S.Levels.FindRef(TEXT("Swordman"))),FMath::Max(1,S.Levels.FindRef(TEXT("Archer"))),SiegeCount,Account->GetAmount(ERTSResource::Wood),Account->GetAmount(ERTSResource::Food));
 }
 if(S.Deployed && (Elapsed>=S.RetreatAt || Army.Num()<2))
 {
  S.Deployed=false;S.NextRaid=Elapsed+300;
  for(auto* U:Army){FRTSOrder O;O.Type=ERTSOrderType::Move;O.TargetLocation=Hall->GetActorLocation()+FVector(-700,0,0);U->ReceiveOrder(O);}
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RAID_WITHDRAW faction=%d day=%d army=%d"),F,Day+1,Army.Num());
 }
 // Commit only part of the army: a home guard stays behind, so a raid pressures the
 // player without turning every day into a one-sided wipe.
 if(S.Deployed){const int32 Strike=FMath::Max(3,Army.Num()/2);for(int32 I=0;I<Strike && I<Army.Num();++I)
 {
  ARTSUnit* U=Army[I];
  if(!U->IsAlive())continue;
  if(const auto* Acquisition=U->FindComponentByClass<URTSAcquisitionComponent>();Acquisition && Acquisition->IsSupportingAlly())continue;
  if(U->GetHealth()->GetHealthPercent()<.3f){FRTSOrder R;R.Type=ERTSOrderType::Move;R.TargetLocation=Hall->GetActorLocation()+FVector(-700,0,0);U->ReceiveOrder(R);continue;}
  // Arriving is not a siege: pick the nearest hostile thing and actually attack it.
  AActor* Target=nullptr;double Nearest=FMath::Square(1500.f);
  for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
   if(It->IsAlive() && It->GetTeam()==0){const double D=FVector::DistSquared2D(It->GetActorLocation(),U->GetActorLocation());if(D<Nearest){Nearest=D;Target=*It;}}
  if(!Target)for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)
   if(It->IsAlive() && It->GetTeam()==0){const double D=FVector::DistSquared2D(It->GetActorLocation(),U->GetActorLocation());if(D<Nearest){Nearest=D;Target=*It;}}
  const auto& Current=U->GetCurrentOrder();
  if(Target)
  {
   if(Current.Type!=ERTSOrderType::Attack || Current.TargetActor.Get()!=Target)
   {FRTSOrder O;O.Type=ERTSOrderType::Attack;O.TargetActor=Target;U->ReceiveOrder(O);}
   continue;
  }
  if(Current.Type!=ERTSOrderType::Attack && Current.Type!=ERTSOrderType::AttackMove)
  {FRTSOrder O;O.Type=ERTSOrderType::AttackMove;O.TargetLocation=Plan.Target;U->ReceiveOrder(O);}
 }
}
}
