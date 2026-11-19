#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Campaign/RTSCampaignBuildingGround.h"
#include "Campaign/RTSCampaignPopulation.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSProjectile.h"
#include "Core/RTSHumanController.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSOrderLibrary.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "AIController.h"
#include "Navigation/CrowdManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "NavigationSystem.h"

namespace
{
 const FName StagedTag(TEXT("RTS_CampaignRestoreStaged"));
 void Stage(AActor* Actor)
 {
  Actor->Tags.AddUnique(StagedTag);Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);Actor->SetActorTickEnabled(false);
  TArray<UActorComponent*> Components;Actor->GetComponents(Components);
  for(auto* Component:Components){Component->SetComponentTickEnabled(false);if(auto* Primitive=Cast<UPrimitiveComponent>(Component))Primitive->SetCanEverAffectNavigation(false);}
 }
 // Restored buildings have yaw only. Test their real boxes on all four planar
 // separating axes, rather than rejecting overlapping world-axis bounds.
 bool BuildingsOverlap(const ARTSBuilding* A,const ARTSBuilding* B)
 {
  const FVector EA=ARTSBuilding::CollisionExtent(A->GetBuildingId(),A->GetFootprint())-FVector(1,1,2);
  const FVector EB=ARTSBuilding::CollisionExtent(B->GetBuildingId(),B->GetFootprint())-FVector(1,1,2);
  const FVector Delta=B->GetActorLocation()-A->GetActorLocation();
  if(FMath::Abs(Delta.Z)>=EA.Z+EB.Z)return false;
  const FVector AX=A->GetActorQuat().GetAxisX(),AY=A->GetActorQuat().GetAxisY();
  const FVector BX=B->GetActorQuat().GetAxisX(),BY=B->GetActorQuat().GetAxisY();
  for(const FVector& Axis:{AX,AY,BX,BY})
  {
   const double ReachA=EA.X*FMath::Abs(FVector::DotProduct(AX,Axis))+EA.Y*FMath::Abs(FVector::DotProduct(AY,Axis));
   const double ReachB=EB.X*FMath::Abs(FVector::DotProduct(BX,Axis))+EB.Y*FMath::Abs(FVector::DotProduct(BY,Axis));
   if(FMath::Abs(FVector::DotProduct(Delta,Axis))>=ReachA+ReachB)return false;
  }
  return true;
 }
}
URTSCampaignSave* ARTSCampaign::CaptureSnapshot(FString& Error) const
{
 Error.Reset();auto Fail=[&](const TCHAR* Why)->URTSCampaignSave*{Error=Why;return nullptr;};
 auto* Balance=URTSBalanceSubsystem::Get(this);auto* Stock=Balance?Balance->GetResources(0):nullptr;
 if(!Stock)return Fail(TEXT("库存账户未就绪，未写入存档。"));
 auto* S=NewObject<URTSCampaignSave>();S->Version=URTSCampaignSave::SnapshotVersion;
 S->CampaignId=CampaignId;S->Elapsed=Elapsed;S->SwordLevel=SwordLevel;S->ArcherLevel=ArcherLevel;S->RegionMask=RegionMask;S->FacilityMask=FacilityMask;S->SpawnDay=SpawnDay;S->SpawnMask=SpawnMask;S->Completed=bCompleted;
 S->PopulationPurchases=PopulationPurchases;
 S->LegacyPopulationBonus=LegacyPopulationBonus;
 S->RegionSpawnValid=RegionSpawnValid;S->GuideStep=GuideStep;S->RaidMask=RaidMask;S->RaidDay=RaidDay;S->ResearchMask=ResearchMask;S->SeenGuideEvents=SeenGuideEvents;
 S->UnitLevels=UnitLevels;S->DailyGeneratedDay=DailyGeneratedDay;SnapshotDailySites(S->DailySites);
 S->PendingFacility=PendingFacility;S->FacilitySeconds=PendingFacility>=0?FacilitySeconds:0;S->SimulationRemainder=Accumulator;S->RefugeeSeconds=RefugeeTimer;S->RegionCapture=RegionCapture;
 S->RaidWaves=RaidWaves;if(S->RaidWaves.IsEmpty())S->RaidWaves.Init(0,RTSCampaignFactions::FactionCount);
 for(const auto& Site:DailySites){if(!Site.Node.IsValid())return Fail(TEXT("每日资源节点未就绪。"));S->DailyCapture.Add(Site.Node->CaptureOwnership());}
 for(int32 I=0;I<static_cast<int32>(ERTSResource::Count);++I)S->Stock.Add(static_cast<ERTSResource>(I),Stock->GetAmount(static_cast<ERTSResource>(I)));
 S->FactionEconomies=EconomyStates;
 for(int32 F=0;F<S->FactionEconomies.Num();++F)for(int32 R=0;R<static_cast<int32>(ERTSResource::Count);++R)S->FactionEconomies[F].Stock.Add(static_cast<ERTSResource>(R),FactionAccounts[F]->GetAmount(static_cast<ERTSResource>(R)));
 for(int32 I=0;I<EnemyNodes.Num();++I){auto* N=EnemyNodes[I].Get();FRTSEnemyNodeSave E;E.Faction=EnemyNodeFactions[I];E.Type=N->GetResourceType();E.Feet=N->GetActorLocation()-FVector(0,0,90);E.Remaining=N->GetRemaining();E.Remainder=N->GetRecoveryRemainder();E.Owner=N->GetOwningTeam();E.Capture=N->CaptureOwnership();S->EnemyNodes.Add(E);}
 TArray<ARTSUnit*> Units;TArray<ARTSBuilding*> SavedBuildings;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->IsAlive() && !It->ActorHasTag(StagedTag))Units.Add(*It);
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->IsAlive() && (It->GetTeam()==0 || GetActorFaction(*It)>=0) && !It->ActorHasTag(StagedTag))SavedBuildings.Add(*It);
 for(auto* B:SavedBuildings)
 {
  FRTSCampaignBuildingWorkSave Work;Work.Progress=B->GetBuildProgress();Work.Facing=B->GetActorRotation().Yaw;
  if(!B->GetProducer()->CaptureProduction(Work.Production))return Fail(TEXT("生产队列正在处理，请稍候保存。"));
  if(auto* Food=B->GetFoodSource()){Work.bHasFoodSource=true;Work.FoodFeet=Food->GetActorLocation()-FVector(0,0,90);Work.FoodRemaining=Food->GetRemaining();Work.FoodRemainder=Food->GetRecoveryRemainder();}
  S->BuildingKinds.Add(B->GetBuildingId());S->BuildingPositions.Add(B->GetActorLocation());S->BuildingHealth.Add(B->GetHealth()->GetHealthPercent());S->BuildingWork.Add(MoveTemp(Work));
  S->BuildingTeams.Add(B->GetTeam());S->BuildingFactions.Add(GetActorFaction(B));
 }
 TMap<const FRTSMoveGroup*,int32> Groups;
 for(auto* Unit:Units)
 {
  if(!Unit->GetUnitData())return Fail(TEXT("单位数值未就绪。"));
  FRTSCampaignUnitSave U;U.Kind=Unit->GetUnitData()->BalanceId;U.Team=Unit->GetTeam();U.Feet=Unit->GetActorLocation()-FVector(0,0,Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
  U.HealthFraction=Unit->GetHealth()->GetHealthPercent();U.MaxHealth=Unit->GetHealth()->GetMaxHealth();U.Damage=Unit->GetUnitData()->AttackDamage;
  U.Encounter=EncounterIds.Contains(Unit)?EncounterIds.FindRef(Unit):-1;U.Faction=Factions.FindRef(Unit);U.Stance=Unit->GetCombatStance();U.CollisionRadius=Unit->GetCapsuleComponent()->GetUnscaledCapsuleRadius();U.Facing=Unit->GetActorRotation().Yaw;
  const auto& Order=Unit->GetCurrentOrder();U.OrderType=Order.Type;U.OrderLocation=Order.TargetLocation;
  // The economic schema has no combat-resume state. A calm attack-move is saved as
  // an ordinary move to the same destination; never reject an otherwise safe save.
  if(U.OrderType==ERTSOrderType::AttackMove)U.OrderType=ERTSOrderType::Move;
  // Completion and destruction are observed by worker ticks at 10 Hz. Saving between
  // those ticks must capture the next ordinary state, not an impossible Build target.
  if(Order.Type==ERTSOrderType::Build)
  {
   const auto* Target=Cast<ARTSBuilding>(Order.TargetActor.Get());
   if(!Target || !Target->IsAlive() || Target->IsComplete())U.OrderType=ERTSOrderType::Stop;
  }
  if(Order.Type==ERTSOrderType::Gather && !IsValid(Order.TargetActor.Get()))U.OrderType=Unit->GetGatherer()->GetCarriedAmount()>0?ERTSOrderType::ReturnResources:ERTSOrderType::Stop;
  if(Unit->GetUnitData()->bWorker)U.Economy=Unit->GetGatherer()->CaptureEconomy();
  if(U.OrderType==ERTSOrderType::Move)
  {
   if(const auto* Group=Unit->GetMoveGroup())
   {
    if(!Groups.Contains(Group))Groups.Add(Group,Groups.Num());U.MoveGroup=Groups.FindRef(Group);U.MoveGoal=Group->bHasRequestedGoal?Group->RequestedGoal:Group->Center;
   }
  }
  if(U.OrderType==ERTSOrderType::Gather || U.OrderType==ERTSOrderType::Build || U.OrderType==ERTSOrderType::ReturnResources)
  {
   AActor* Target=Order.TargetActor.Get();
   if(Target)
   {
    for(int32 I=0;I<Nodes.Num();++I)if(Nodes[I]==Target){U.TargetClass=1;U.TargetIndex=I;break;}
    if(U.TargetClass==0)for(const auto& Site:DailySites)if(Site.Node.Get()==Target){U.TargetClass=2;U.TargetIndex=Site.State.Id;break;}
    if(U.TargetClass==0){const int32 I=SavedBuildings.IndexOfByKey(Cast<ARTSBuilding>(Target));if(I!=INDEX_NONE){U.TargetClass=3;U.TargetIndex=I;}}
    if(U.TargetClass==0)for(int32 I=0;I<SavedBuildings.Num();++I)if(SavedBuildings[I]->GetFoodSource()==Target){U.TargetClass=5;U.TargetIndex=I;break;}
    if(U.TargetClass==0)for(int32 I=0;I<EnemyNodes.Num();++I)if(EnemyNodes[I]==Target){U.TargetClass=6;U.TargetIndex=I;break;}
    if(U.TargetClass==0)return Fail(TEXT("工作目标不属于可恢复的战役资源或建筑；当前状态未写入。"));
   }
  }
  S->Units.Add(MoveTemp(U));
 }
 for(const auto& C:Camps)S->Camps.Add(C.State);
 for(ARTSResourceNode* N:Nodes)
 {
  if(!IsValid(N))return Fail(TEXT("资源节点已失效，请稍候重试。"));
  S->NodeRemaining.Add(N->GetRemaining());S->NodeRemainders.Add(N->GetRecoveryRemainder());S->NodeTiers.Add(N->GetResourceTier());S->NodeOwners.Add(N->GetOwningTeam());
  S->NodeCapture.Add(N->CaptureOwnership());
 }
 return S;
}
bool ARTSCampaign::PreflightSnapshot(const URTSCampaignSave* S,FString& Error) const
{
 auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
 if(!Nav || !URTSBalanceSubsystem::Get(this) || !URTSBalanceSubsystem::Get(this)->GetResources(0)){Error=TEXT("导航或资源账户未就绪，当前战役未改变。");return false;}
 for(const auto& U:S->Units)
 {
  FNavLocation P;const bool Projected=Nav->ProjectPointToNavigation(U.Feet,P,FVector(70,70,120));
  if(!Projected || FMath::Abs(P.Location.Z-U.Feet.Z)>65 || FVector::Dist2D(P.Location,U.Feet)>50)
  {UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_LOAD_NAV_REJECT kind=%s feet=%s version=%d"),*U.Kind.ToString(),*U.Feet.ToString(),S->Version);Error=TEXT("存档单位位置不可导航，当前战役未改变。");return false;}
 }
 for(int32 Index=0;Index<S->EnemyNodes.Num();++Index)
 {
  const auto& N=S->EnemyNodes[Index];
  // A solid resource intentionally removes navigation at its own centre. Validate
  // its supporting ground and actual outside work positions, never a pawn inside it.
  FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(CampaignNodeGround),false);
  const bool Supported=GetWorld()->LineTraceSingleByObjectType(Ground,N.Feet+FVector(0,0,65),N.Feet-FVector(0,0,65),FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Ground.ImpactNormal.Z>=.7f;
  int32 WorkPositions=0;
  for(int32 I=0;Supported && I<12;++I)
  {
   const double A=I*UE_PI/6;const FVector At=N.Feet+FVector(FMath::Cos(A)*120,FMath::Sin(A)*120,0);FNavLocation P;
   if(Nav->ProjectPointToNavigation(At,P,FVector(20,20,120)) && FVector::Dist2D(P.Location,At)<=15 && FMath::Abs(P.Location.Z-N.Feet.Z)<=65)++WorkPositions;
  }
  if(!Supported || WorkPositions<2)
  {UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_NODE_NAV_REJECT index=%d faction=%d type=%d feet=%s ground=%d workPositions=%d"),Index,N.Faction,int32(N.Type),*N.Feet.ToString(),Supported,WorkPositions);Error=TEXT("敌军资源点缺少地面支撑或可达采集位置，当前战役未改变。");return false;}
 }
 return true;
}
bool ARTSCampaign::RestoreSnapshot(const URTSCampaignSave* S,FString& Error)
{
 auto* World=GetWorld();auto* Balance=URTSBalanceSubsystem::Get(this);
 FTransform Identity;auto* Pending=World->SpawnActorDeferred<ARTSCampaign>(StaticClass(),Identity,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!Pending){Error=TEXT("无法准备读取事务，当前战役未改变。");return false;}
 Pending->bStagingRestore=true;Pending->FinishSpawning(Identity);Pending->SetActorTickEnabled(false);
 bool bCommitted=false;
 ON_SCOPE_EXIT
 {
  if(!bCommitted)for(AActor* Actor:Pending->Owned)if(IsValid(Actor)){Actor->SetActorEnableCollision(false);Actor->Destroy();}
  Pending->Owned.Reset();Pending->Destroy();
 };
 auto Fail=[&](const TCHAR* Why){Error=FString(Why)+TEXT(" 当前战役与磁盘记录未改变。");return false;};
 auto SetScalars=[S](ARTSCampaign* C)
 {
  C->CampaignId=S->CampaignId;C->Elapsed=S->Elapsed;C->SwordLevel=S->SwordLevel;C->ArcherLevel=S->ArcherLevel;C->RegionMask=S->RegionMask;C->FacilityMask=S->FacilityMask;C->SpawnDay=S->SpawnDay;C->SpawnMask=S->SpawnMask;C->bCompleted=S->Completed;
  C->PopulationPurchases=S->Version>=14?S->PopulationPurchases:0;
  C->LegacyPopulationBonus=RTSCampaignPopulation::RestoredBonus(S->Version,(S->FacilityMask&1)!=0,S->LegacyPopulationBonus);
  C->UnitLevels=S->UnitLevels;C->DailyGeneratedDay=S->DailyGeneratedDay;C->RegionSpawnValid=S->RegionSpawnValid;C->GuideStep=S->GuideStep;C->RaidMask=S->RaidMask;C->RaidDay=S->RaidDay;C->ResearchMask=S->ResearchMask;C->SeenGuideEvents=S->SeenGuideEvents;
  C->PendingFacility=S->Version>=12?S->PendingFacility:-1;C->FacilitySeconds=S->Version>=12?S->FacilitySeconds:0;C->Accumulator=S->Version>=12?S->SimulationRemainder:0;C->RefugeeTimer=S->Version>=12?S->RefugeeSeconds:0;
  C->RaidWaves=S->RaidWaves;if(S->Version<12)C->RaidWaves.Init(0,RTSCampaignFactions::FactionCount);
  C->RegionCapture=S->RegionCapture;if(S->Version<12)C->RegionCapture.Init(0,RTSCampaignFactions::RegionCount);
 };
 SetScalars(Pending);Pending->RegionGuards.SetNum(RTSCampaignFactions::RegionCount);
 if(S->Version>=13)
 {
  Pending->EconomyStates=S->FactionEconomies;
  for(const auto& State:S->FactionEconomies){auto* A=NewObject<URTSResourceComponent>(this);A->SetAmounts(State.Stock);Pending->FactionAccounts.Add(A);}
  for(const auto& N:S->EnemyNodes)if(!Pending->RestoreEnemyNode(N))return Fail(TEXT("敌军资源恢复失败。"));
 }
 auto MakeBuilding=[&](FName Kind,const FVector& Position,float Health,const FRTSCampaignBuildingWorkSave& Work,uint8 Team,int32 Faction)->ARTSBuilding*
 {
  FVector RestorePosition=Position;
  const FVector OldSlopeSite=RTSCampaignTerrain::Ground(RTSCampaignFactions::Base[2]+FVector(0,3100,0));
  if(Team==1 && Faction==2 && Kind==TEXT("ArcheryRange") && Position.Equals(OldSlopeSite,1.f))
  {
   RestorePosition=RTSCampaignTerrain::Ground(RTSCampaignFactions::Base[2]+FVector(1000,1100,0));
   UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RESTORE_SLOPE_SITE old=%s new=%s"),*Position.ToString(),*RestorePosition.ToString());
  }
  // Fixed-site buildings carry the map collision height; only those exact sites are
  // adjusted in memory, while arbitrary or player-authored positions stay immutable.
  if(RTSCampaignBuildingGround::IsFormulaSite(Team,Faction,Kind,RestorePosition,Work.Facing))
  {
   FVector Grounded;
   if(!RTSCampaignBuildingGround::Sample(World,Kind,RestorePosition,Work.Facing,Grounded))return nullptr;
   if(!Grounded.Equals(RestorePosition,.01))UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RESTORE_GROUND_SITE faction=%d kind=%s old=%s new=%s"),Faction,*Kind.ToString(),*RestorePosition.ToString(),*Grounded.ToString());
   RestorePosition=Grounded;
  }
  FTransform T(FRotator(0,Work.Facing,0),RestorePosition);auto* B=World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!B)return nullptr;Stage(B);Pending->Owned.Add(B);B->Configure(Kind,Team,Work.Progress>=1);B->FinishSpawning(T);Stage(B);
  if(Faction>=0)Pending->EconomicFactions.Add(B,Faction);
  if(!B->RestoreConstructionProgress(Work.Progress) || !B->GetProducer()->RestoreProduction(Work.Production,Faction>=0?Pending->FactionAccounts[Faction].Get():Balance->GetResources(0)))return nullptr;
  if(Work.bHasFoodSource){if(!B->RestoreFoodSource(Work.FoodFeet,Work.FoodRemaining,Work.FoodRemainder,true))return nullptr;Stage(B->GetFoodSource());Pending->Owned.Add(B->GetFoodSource());}
  B->GetHealth()->RestoreHealthFraction(Health);Pending->Buildings.Add(B);return B;
 };
 for(int32 I=0;I<S->BuildingKinds.Num();++I)if(!MakeBuilding(S->BuildingKinds[I],S->BuildingPositions[I],S->BuildingHealth[I],S->Version>=12?S->BuildingWork[I]:FRTSCampaignBuildingWorkSave(),S->Version>=13?S->BuildingTeams[I]:0,S->Version>=13?S->BuildingFactions[I]:-1))return Fail(TEXT("建筑或已付费队列恢复失败。"));
 Pending->CreateNodes();if(Pending->Nodes.Num()!=8)return Fail(TEXT("资源节点恢复失败。"));
 Pending->EnsureVineyardNode(S->NodeRemaining.Num()>8 || S->Elapsed>=VineyardSpawnElapsed);
 if(S->NodeRemaining.Num()>Pending->Nodes.Num())return Fail(TEXT("葡萄园恢复失败。"));
 for(int32 I=0;I<S->NodeRemaining.Num();++I)
 {
  auto* N=Pending->Nodes[I].Get();if(S->NodeTiers.IsValidIndex(I))N->ConfigureResourceTier(S->NodeTiers[I],I==8);
  if(S->NodeOwners.IsValidIndex(I))N->SetOwningTeam(S->NodeOwners[I]);N->RestoreEconomyState(S->NodeRemaining[I],S->NodeRemainders[I]);
  if(N->GetResourceType()==ERTSResource::IronOre && N->GetOwningTeam()==254)N->SetOwningTeam(RTSTeam::Neutral);
  if(S->Version>=12 && !N->RestoreOwnership(S->NodeCapture[I]))return Fail(TEXT("资源点夺取进度恢复失败。"));
 }
 if(Pending->RestoreDailySites(S->DailySites)!=0)return Fail(TEXT("每日资源点恢复失败。"));
 if(S->Version>=12)for(int32 I=0;I<Pending->DailySites.Num();++I)if(!Pending->DailySites[I].Node->RestoreOwnership(S->DailyCapture[I]))return Fail(TEXT("每日资源点夺取进度恢复失败。"));
 for(const auto& State:S->Camps){FRTSCampaignCamp C;C.State=State;if(!State.Won){C.Marker=Pending->Marker(State.Center,TEXT("每日小据点"),FLinearColor::Red);if(!C.Marker.IsValid())return Fail(TEXT("据点标记恢复失败。"));}Pending->Camps.Add(MoveTemp(C));}
 TArray<ARTSUnit*> RestoredUnits;
 for(const auto& Saved:S->Units)
 {
  auto* U=Pending->Spawn(Saved.Kind,Saved.Team,Saved.Feet,Saved.Team?Saved.MaxHealth:0,Saved.Team?Saved.Damage:0);
  if(!U)return Fail(TEXT("单位或必需模型恢复失败。"));
  Stage(U);U->SetActorRotation(FRotator(0,Saved.Facing,0));if(S->Version>=12)U->GetCapsuleComponent()->SetCapsuleRadius(Saved.CollisionRadius,false);
  U->GetHealth()->RestoreHealthFraction(Saved.HealthFraction);U->SetCombatStance(S->Version>=12?Saved.Stance:ERTSCombatStance::Patrol);RestoredUnits.Add(U);
  if(Saved.Team==1)
  {
   Pending->ApplyFaction(U,Saved.Faction);Pending->EncounterIds.Add(U,Saved.Encounter);
   if(Saved.Encounter<RTSCampaignFactions::RegionCount)Pending->RegionGuards[Saved.Encounter].Add(U);
   else if(Saved.Encounter>=30000){Pending->EconomicFactions.Add(U,Saved.Faction);Pending->ApplyEnemyProgression(U,FMath::Max(1,Pending->EconomyStates[Saved.Faction].Levels.FindRef(Saved.Kind))-1);}
   else if(Saved.Encounter>=10000)Pending->RegisterDailyUnit(U,Saved.Encounter);
   else for(auto& C:Pending->Camps)if(C.State.Id==Saved.Encounter-100)C.Guards.Add(U);
  }
 }
 if(S->Version<13 && S->Units.Num()+8>200)return Fail(TEXT("旧存档迁移超过200单位上限。"));
 if(S->Version<13 && !Pending->BootstrapEnemyEconomy())return Fail(TEXT("敌方基地地面恢复失败。")); // staged migration, never writes the slot
 // A defeated headquarters leaves nothing behind: restore a base flag only while that base still stands.
 for(int32 R=3;R<RTSCampaignFactions::RegionCount;++R)
 {
  if(S->RegionMask&(1<<R))continue;
  if(!Pending->Marker(RTSCampaignFactions::RegionCenters[R],FString::Printf(TEXT("%s · %s"),RTSCampaignFactions::Name[RTSCampaignFactions::RegionFaction[R]],TEXT("摧毁敌方大本营")),FLinearColor::White))return Fail(TEXT("区域标记恢复失败。"));
 }
 int32 StagedLiving=0;for(AActor* A:Pending->Owned)if(auto* U=Cast<ARTSUnit>(A))StagedLiving+=U->IsAlive();if(StagedLiving>200)return Fail(TEXT("迁移单位上限超出。"));
 if(Pending->Buildings.Num()>30)return Fail(TEXT("迁移建筑超过30上限。"));
 for(AActor* A:Pending->Owned)if(auto* U=Cast<ARTSUnit>(A))RestoredUnits.AddUnique(U);
 for(AActor* Actor:Pending->Owned)if(IsValid(Actor))Stage(Actor);
 // Ignore the old generation, not the terrain or unrelated blocking actors.
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CampaignRestorePreflight),false);
 for(AActor* Actor:Owned)if(IsValid(Actor))Query.AddIgnoredActor(Actor);
 for(TActorIterator<ARTSUnit> It(World);It;++It)if(It->GetTeam()==0)Query.AddIgnoredActor(*It);
 for(TActorIterator<ARTSBuilding> It(World);It;++It)if(It->GetTeam()==0)Query.AddIgnoredActor(*It);
 for(AActor* Actor:Pending->Owned)if(IsValid(Actor))Query.AddIgnoredActor(Actor);
 for(int32 I=0;I<Pending->Buildings.Num();++I)
 {
  ARTSBuilding* B=Pending->Buildings[I];const auto E=ARTSBuilding::CollisionExtent(B->GetBuildingId(),B->GetFootprint());
  if(World->OverlapBlockingTestByChannel(B->GetActorLocation()+FVector(0,0,100),B->GetActorQuat(),ECC_Pawn,FCollisionShape::MakeBox(E-FVector(1,1,2)),Query))
  {
   UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_RESTORE_BUILDING_BLOCKED index=%d kind=%s team=%d position=%s"),I,*B->GetBuildingId().ToString(),B->GetTeam(),*B->GetActorLocation().ToString());
   return Fail(TEXT("存档建筑位置被地形或其他对象阻挡。"));
  }
  for(int32 J=0;J<I;++J)if(BuildingsOverlap(B,Pending->Buildings[J]))return Fail(TEXT("存档建筑占位冲突，无法载入。"));
 }
 // Reasons: 0 clear, 1 world/terrain block, 2 another restored body, 3 a restored building.
 auto CapsuleClear=[&](const FVector& At,const float Radius,const float Half,const int32 Prior,int32& Why)->bool
 {
  Why=0;
  if(World->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius-1,Half-2),Query)){Why=1;return false;}
  for(int32 J=0;J<Prior;++J)
  {
   auto* V=RestoredUnits[J];auto* Other=V->GetCapsuleComponent();const float OtherRadius=Other->GetScaledCapsuleRadius();
   const double DZ=FMath::Max(0.,FMath::Abs(At.Z-V->GetActorLocation().Z)-(Half-Radius)-(Other->GetScaledCapsuleHalfHeight()-OtherRadius));
   if(FVector::DistSquared2D(At,V->GetActorLocation())+DZ*DZ<FMath::Square(Radius+OtherRadius-2)){Why=2;return false;}
  }
  for(ARTSBuilding* B:Pending->Buildings)
  {
   const FVector E=ARTSBuilding::CollisionExtent(B->GetBuildingId(),B->GetFootprint());const FVector P=B->GetActorQuat().UnrotateVector(At-B->GetActorLocation()-FVector(0,0,100));
   const double X=FMath::Max(0.,FMath::Abs(P.X)-E.X),Y=FMath::Max(0.,FMath::Abs(P.Y)-E.Y),Z=FMath::Max(0.,FMath::Abs(P.Z)-(Half-Radius)-E.Z);
   if(X*X+Y*Y+Z*Z<FMath::Square(Radius-2)){Why=3;return false;}
  }
  return true;
 };
 // A record can carry bodies that overlap each other. Only that internal conflict is
 // repaired, in memory and deterministically; terrain and world intrusions still reject
 // the record because the world would not match it.
 int32 Separated=0;
 for(int32 I=0;I<RestoredUnits.Num();++I)
 {
  auto* U=RestoredUnits[I];auto* Capsule=U->GetCapsuleComponent();const float Radius=Capsule->GetScaledCapsuleRadius(),Half=Capsule->GetScaledCapsuleHalfHeight();
  const FVector Recorded=U->GetActorLocation();int32 Reason=0;
  if(CapsuleClear(Recorded,Radius,Half,I,Reason))continue;
  if(Reason!=2)return Fail(Reason==1?TEXT("存档单位位置被地形或其他对象阻挡。"):TEXT("存档单位与建筑占位重叠。"));
  bool bPlaced=false;FVector Target=Recorded;
  for(int32 Step=1;!bPlaced && Step<=24;++Step)
  {
   const double Outer=Step*40.0;
   for(int32 K=0;K<12;++K)
   {
    const double A=(K+Step*0.5)*UE_PI/6;const FVector At=Recorded+FVector(FMath::Cos(A)*Outer,FMath::Sin(A)*Outer,0);int32 Candidate=0;
    // Only a spot clear of terrain, buildings and already placed bodies is acceptable.
    if(CapsuleClear(At,Radius,Half,I,Candidate)){Target=At;bPlaced=true;break;}
   }
  }
  if(!bPlaced)return Fail(TEXT("存档单位占位冲突，附近没有可用位置。"));
  UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_RESTORE_UNIT_SEPARATED index=%d kind=%s team=%d recorded=%s placed=%s offset=%.0f"),I,*U->GetUnitData()->BalanceId.ToString(),U->GetTeam(),*Recorded.ToString(),*Target.ToString(),FVector::Dist2D(Recorded,Target));
  U->SetActorLocation(Target,false,nullptr,ETeleportType::TeleportPhysics);
  int32 After=0;if(!CapsuleClear(Target,Radius,Half,I,After))return Fail(TEXT("存档单位摆放冲突，无法载入。"));
  ++Separated;
 }
 if(Separated>0)UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RESTORE_UNIT_SEPARATION separated=%d recordLayoutPreserved=1 worldIntrusionsStillRejected=1"),Separated);
 if(S->Version>=12)
 {
  TMap<int32,TArray<ARTSUnit*>> Movers;
  for(int32 I=0;I<S->Units.Num();++I)
  {
   auto* U=RestoredUnits[I];const auto& Saved=S->Units[I];FRTSOrder O;O.Type=Saved.OrderType;O.TargetLocation=Saved.OrderLocation;
   if(Saved.TargetClass==1)O.TargetActor=Pending->Nodes[Saved.TargetIndex];
   if(Saved.TargetClass==2)for(auto& Site:Pending->DailySites)if(Site.State.Id==Saved.TargetIndex)O.TargetActor=Site.Node.Get();
   if(Saved.TargetClass==3)O.TargetActor=Pending->Buildings[Saved.TargetIndex];
   if(Saved.TargetClass==4)O.TargetActor=RestoredUnits[Saved.TargetIndex];
   if(Saved.TargetClass==5)O.TargetActor=Pending->Buildings[Saved.TargetIndex]->GetFoodSource();
   if(Saved.TargetClass==6)O.TargetActor=Pending->EnemyNodes[Saved.TargetIndex];
   if(Saved.MoveGroup>=0)Movers.FindOrAdd(Saved.MoveGroup).Add(U);else U->ReceiveOrder(O);
   if(U->GetUnitData()->bWorker && !U->GetGatherer()->RestoreEconomy(Saved.Economy))return Fail(TEXT("工人携货或工作进度恢复失败。"));
   if(Saved.MoveGroup<0 && Saved.OrderType==ERTSOrderType::Move && U->GetCurrentOrder().Type!=ERTSOrderType::Move)return Fail(TEXT("移动任务暂时不可恢复。"));
  }
  for(auto& Pair:Movers)
  {
   const int32 I=RestoredUnits.IndexOfByKey(Pair.Value[0]);URTSOrderLibrary::IssueMoveOrder(Pending,Pair.Value,S->Units[I].MoveGoal);
   // Group orders reset transient work state; cargo and fractional work remain immutable.
   for(auto* U:Pair.Value)if(U->GetUnitData()->bWorker && !U->GetGatherer()->RestoreEconomy(S->Units[RestoredUnits.IndexOfByKey(U)].Economy))return Fail(TEXT("编队工人携货恢复失败。"));
   // A group that lost members restores as a plain move: a formation handle is transient
   // state, not part of the record.
   const bool bFormation=Pair.Value.Num()>1;
   for(auto* U:Pair.Value)
   {
    if(U->GetCurrentOrder().Type==ERTSOrderType::Move && (!bFormation || U->GetMoveGroup()))continue;
    auto* AI=Cast<AAIController>(U->GetController());
    UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_RESTORE_GROUP_FALLBACK unit=%s kind=%s members=%d order=%d move=%d position=%s goal=%s"),*U->GetName(),*U->GetUnitData()->BalanceId.ToString(),Pair.Value.Num(),int32(U->GetCurrentOrder().Type),AI?int32(AI->GetMoveStatus()):-1,*U->GetActorLocation().ToString(),*S->Units[RestoredUnits.IndexOfByKey(U)].MoveGoal.ToString());
    FRTSOrder FallbackOrder;FallbackOrder.Type=ERTSOrderType::Move;FallbackOrder.TargetLocation=S->Units[RestoredUnits.IndexOfByKey(U)].MoveGoal;U->ReceiveOrder(FallbackOrder);
   }
  }
 }
 // Staged pawns must not occupy Detour slots while the old generation is live.
 auto* CrowdManager=UCrowdManager::GetCurrent(World);
 if(!CrowdManager)return Fail(TEXT("移动代理管理器尚未就绪，当前战役未改变。"));
 for(auto* U:RestoredUnits)
 {
  auto* AI=Cast<AAIController>(U->GetController());auto* Crowd=AI?Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()):nullptr;
  if(!Crowd || Crowd->GetCrowdSimulationState()!=ECrowdSimulationState::Disabled || CrowdManager->IsAgentValid(Crowd))return Fail(TEXT("暂存移动代理隔离失败，当前战役未改变。"));
 }
 // All fallible reconstruction finished. Only now retire the old generation.
 if(auto* PC=Cast<ARTSHumanController>(World->GetFirstPlayerController())){PC->CancelBuildingPlacement();PC->GetSelection()->ClearSelection();PC->SelectBuilding(nullptr);}
 for(TActorIterator<ARTSProjectile> It(World);It;++It)It->Destroy();
 ClearOwned();SetScalars(this);bDefeated=false;
 Owned=MoveTemp(Pending->Owned);Buildings=MoveTemp(Pending->Buildings);Nodes=MoveTemp(Pending->Nodes);Camps=MoveTemp(Pending->Camps);DailySites=MoveTemp(Pending->DailySites);RegionGuards=MoveTemp(Pending->RegionGuards);
 Factions=MoveTemp(Pending->Factions);EncounterIds=MoveTemp(Pending->EncounterIds);AppliedLevels=MoveTemp(Pending->AppliedLevels);AppliedEnemyLevels=MoveTemp(Pending->AppliedEnemyLevels);
 FactionAccounts=MoveTemp(Pending->FactionAccounts);EconomyStates=MoveTemp(Pending->EconomyStates);EconomicFactions=MoveTemp(Pending->EconomicFactions);EnemyNodes=MoveTemp(Pending->EnemyNodes);EnemyNodeFactions=MoveTemp(Pending->EnemyNodeFactions);
 // Legacy flag captures cannot defeat a still-standing enemy headquarters.
 for(ARTSBuilding* B:Buildings)if(IsValid(B) && B->IsAlive() && B->GetTeam()==1 && B->GetBuildingId()==TEXT("TownHall"))
 {
  const int32 F=GetActorFaction(B);if(F>=0){RegionMask&=~(1<<RTSCampaignFactions::RegionIndex[F]);bCompleted=false;if(EconomyStates.IsValidIndex(F))EconomyStates[F].Active=true;}
 }
 for(URTSResourceComponent* Account:FactionAccounts)if(Account && Account->GetOuter()!=this)Account->Rename(nullptr,this);
 Vineyard=Pending->Vineyard;bVineyardSpawned=Pending->bVineyardSpawned;VineyardCapture=Pending->VineyardCapture;WarDecisionTimer=0;
 for(AActor* Actor:Owned)if(IsValid(Actor))
 {
  Actor->Tags.Remove(StagedTag);Actor->SetActorHiddenInGame(false);Actor->SetActorEnableCollision(true);Actor->SetActorTickEnabled(Actor->PrimaryActorTick.bCanEverTick);
  TArray<UActorComponent*> Components;Actor->GetComponents(Components);
  for(auto* Component:Components){if(Component->PrimaryComponentTick.bCanEverTick)Component->SetComponentTickEnabled(true);}
  if(auto* B=Cast<ARTSBuilding>(Actor)){TArray<UPrimitiveComponent*> Primitives;B->GetComponents(Primitives);for(auto* P:Primitives)if(P->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)P->SetCanEverAffectNavigation(true);}
  if(auto* Node=Cast<ARTSResourceNode>(Actor))if(auto* Root=Cast<UPrimitiveComponent>(Node->GetRootComponent()))Root->SetCanEverAffectNavigation(true);
 }
 int32 ValidCrowdAgents=0;
 for(auto* U:RestoredUnits)
 {
  auto* AI=CastChecked<AAIController>(U->GetController());auto* Crowd=CastChecked<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent());
  ValidCrowdAgents+=Crowd->ActivateRestoredAgent();
 }
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RESTORE_CROWD units=%d valid=%d stagedRegistered=0"),RestoredUnits.Num(),ValidCrowdAgents);
 ensureMsgf(ValidCrowdAgents==RestoredUnits.Num(),TEXT("Committed campaign units must all have valid Crowd agents"));
 Balance->GetResources(0)->SetAmounts(S->Stock);bCommitted=true;CreateDecorations();
 StartEconomyTrees();
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_RESTORE_COMMIT version=%d units=%d buildings=%d busy=%d"),S->Version,RestoredUnits.Num(),Buildings.Num(),S->Version>=12);
 return true;
}
