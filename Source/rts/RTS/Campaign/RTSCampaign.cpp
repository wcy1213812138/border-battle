#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignPopulation.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignPalette.h"
#include "Campaign/RTSCampaignWidget.h"
#include "Campaign/RTSCampaignEntry.h"
#include "Core/RTSHumanController.h"
#include "Core/RTSPlayerState.h"
#include "Camera/RTSCameraPawn.h"
#include "UI/RTSHUD.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Units/RTSPeasantShowcase.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Selection/RTSSelectionComponent.h"
#include "Match/RTSMatchSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Campaign/RTSWorldLabel.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "AIController.h"

namespace
{
 const FVector Home=ARTSCampaign::HomeLocation();
 using RTSCampaignFactions::RegionCenters;
 const TCHAR* RegionNames[] = {TEXT("赤金沙团南翼前哨 · 解锁谷地铁矿"),TEXT("赤金沙团东翼前哨 · 解锁富铁矿"),TEXT("暮棘盟南翼前哨 · 解锁琥珀矿"),TEXT("暮棘盟边境要塞"),TEXT("赤金沙团边境大营"),TEXT("赤焰卫边境堡垒"),TEXT("蔷薇庭边境营地")};
 const TCHAR* ResourceNames[] = {TEXT("木材"),TEXT("粮食"),TEXT("铁矿"),TEXT("精铁"),TEXT("琥珀"),TEXT("水晶"),TEXT("金币"),TEXT("法力")};
 int32 Capacity(ERTSResource R) { return RTSCampaignProgression::StoreCapacity(R); }
 int32 ResearchBit(FName Kind) { const int32 I=RTSCampaignProgression::Index(Kind);return I>=3?1<<(I-3):0; }
 const int32 NodeTiers[]={1,1,1,2,2,3,1,2};
 const FVector VineyardSpot=RTSCampaignMapSpace::FromLegacy(FVector(-1500,1500,0));
 TMap<ERTSResource,int32> ResearchCost(FName Kind) { return RTSCampaignProgression::ResearchCost(Kind); }
 FString CostText(const TMap<ERTSResource,int32>& Cost)
 {
  FString S;
  for(int32 I=0;I<static_cast<int32>(ERTSResource::Count);++I) { if(const int32* N=Cost.Find(static_cast<ERTSResource>(I));N && *N>0) S+=FString::Printf(TEXT("%s%d "),ResourceNames[I],*N); }
  return S;
 }
 bool Fighting(const ARTSUnit* U)
 {
  return U && U->IsAlive() && (U->GetAttack()->IsSwinging() || U->GetWorld()->GetTimeSeconds()-U->GetHealth()->GetLastDamageTime()<10);
 }
}
ARTSCampaignGameMode::ARTSCampaignGameMode()
{
 DefaultPawnClass=ARTSCameraPawn::StaticClass(); PlayerControllerClass=ARTSHumanController::StaticClass();
 PlayerStateClass=ARTSPlayerState::StaticClass(); HUDClass=ARTSHUD::StaticClass();
}
void ARTSCampaignGameMode::BeginPlay()
{
 Super::BeginPlay();
 if(URTSMatchSubsystem* M=URTSMatchSubsystem::Get(this)) M->EnterSandbox();
 GetWorld()->SpawnActor<ARTSCampaign>();
 if(APlayerController* PC=GetWorld()->GetFirstPlayerController())
  if(auto* Entry=CreateWidget<URTSCampaignEntryWidget>(PC,URTSCampaignEntryWidget::StaticClass()))Entry->AddToViewport(100);
}
ARTSCampaign::ARTSCampaign() { PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=0.2f; }
ARTSCampaign* ARTSCampaign::Get(const UObject* Context)
{
 UWorld* W=Context?Context->GetWorld():nullptr;
 if(W) for(TActorIterator<ARTSCampaign> It(W);It;++It) if(!It->bStagingRestore)return *It;
 return nullptr;
}
void ARTSCampaign::BeginPlay() { Super::BeginPlay();if(bStagingRestore)return;CampaignId=FGuid::NewGuid().ToString(); Message=TEXT("正在准备导航与边境营地……"); }
int32 ARTSCampaign::AppearanceForLevel(int32 L) { return L>=26?5:L>=21?4:L>=11?3:L>=6?2:1; }
int32 ARTSCampaign::GetLevel(FName Kind) const { return FMath::Clamp(UnitLevels.FindRef(Kind)>0?UnitLevels.FindRef(Kind):Kind==TEXT("Swordman")?SwordLevel:Kind==TEXT("Archer")?ArcherLevel:1,1,30); }
ARTSUnit* ARTSCampaign::Spawn(FName Kind,uint8 Team,const FVector& Feet,float HP,float Damage)
{
 URTSBalanceSubsystem* B=URTSBalanceSubsystem::Get(this); if(!B) return nullptr;
 if(!bStagingRestore){int32 Living=0;for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)Living+=It->IsAlive();if(Living>=200)return nullptr;}
 UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
 FNavLocation P; if(!Nav || !Nav->ProjectPointToNavigation(Feet,P,FVector(70,70,120)) || FMath::Abs(P.Location.Z-Feet.Z)>65 || FVector::Dist2D(P.Location,Feet)>50) return nullptr;
 FTransform T((bStagingRestore?Feet:P.Location)+FVector(0,0,82));
 ARTSUnit* U=GetWorld()->SpawnActorDeferred<ARTSUnit>(ARTSPeasantShowcase::StaticClass(),T,nullptr,nullptr,bStagingRestore?ESpawnActorCollisionHandlingMethod::AlwaysSpawn:ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
 if(!U) return nullptr;
 if(bStagingRestore){U->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));U->SetActorEnableCollision(false);U->SetActorHiddenInGame(true);}
 URTSUnitData* D=B->CreateUnitData(Kind,U); if(!D) { U->Destroy(); return nullptr; }
 if(HP>0) D->MaxHealth=HP; if(Damage>0) D->AttackDamage=Damage;
 // Nav polygons approximate slopes and can project below the actual saved floor.
 // Preflight still enforces strict nav proximity; staged collision checks validate
 // the exact saved position instead of silently snapping the unit into terrain.
 if(bStagingRestore)T.SetLocation(Feet+FVector(0,0,D->CapsuleHalfHeight));
 U->Configure(D,Team);
 if(ARTSPeasantShowcase* V=Cast<ARTSPeasantShowcase>(U)) { V->SetPreviewKind(Kind); V->SetPreviewTier(AppearanceForLevel(Team==0?GetLevel(Kind):EnemyLevelForDay(GetDay()))); }
 U->FinishSpawning(T); Owned.Add(U);
 U->SetCombatStance(ERTSCombatStance::Patrol);
 if(Team==0) ApplyProgression(U);
 return U;
}
void ARTSCampaign::ApplyProgression(ARTSUnit* U)
{
 if(!U || !U->IsAlive() || U->GetTeam()!=0 || !U->GetUnitData()) return;
 const FName Kind=U->GetUnitData()->BalanceId;
 if(RTSCampaignProgression::Index(Kind)<0) return;
 const int32 L=GetLevel(Kind);
 if(AppliedLevels.FindRef(U)==L) return;
 const FRTSBalanceRow* R=URTSBalanceSubsystem::Get(this)->Find(Kind); if(!R) return;
 const float M=RTSCampaignProgression::Growth(L);
 const float HP=RTSCampaignProgression::BaseHealth(Kind)*M,Damage=RTSCampaignProgression::BaseDamage(Kind)*M;
 U->GetUnitData()->MaxHealth=HP;U->GetUnitData()->AttackDamage=Damage;U->GetUnitData()->HealAmount=RTSCampaignProgression::BaseHeal(Kind)*M;
 U->GetHealth()->SetMaxHealthPreservingRatio(HP);
 U->GetAttack()->SetStats(Damage,R->AttackRange,R->AttackInterval,R->AttackHitDelay);
 if(ARTSPeasantShowcase* V=Cast<ARTSPeasantShowcase>(U)) V->RefreshTier(AppearanceForLevel(L));
 AppliedLevels.Add(U,L);
}
FName ARTSCampaign::EnemyKindForDay(int32 Day,int32 Slot,bool bRanged)
{
 using namespace RTSCampaignProgression;
 // Ordered by unlock day so "every unlocked kind" is a prefix of the list, exactly like player research.
 static const FName Melee[]={FName("Swordman"),FName("Light_Infantry"),FName("Spearman"),FName("Heavy_Infantry"),FName("HeavySwordman"),FName("Halberdier"),FName("Paladin"),FName("King")};
 static const FName Ranged[]={FName("Archer"),FName("Scout"),FName("Crossbowman"),FName("Mage")};
 const FName* List=bRanged?Ranged:Melee; const int32 Total=bRanged?UE_ARRAY_COUNT(Ranged):UE_ARRAY_COUNT(Melee);
 int32 Unlocked=0;
 for(int32 I=0;I<Total;++I) if(UnlockDay(List[I])<=Day) ++Unlocked;
 const FName Pick=Unlocked>0?List[FMath::Max(0,Slot)%Unlocked]:(bRanged?FName("Archer"):FName("Swordman"));
 // Never let a late kind break a spawn: fall back to the basic kind when the balance table lacks it.
 return Index(Pick)>=0?Pick:(bRanged?FName("Archer"):FName("Swordman"));
}
void ARTSCampaign::ApplyEnemyProgression(ARTSUnit* U,int32 Day,int32 TierBoost)
{
 if(!U || !U->IsAlive() || !U->GetUnitData() || U->GetTeam()==0) return;
 const FName Kind=U->GetUnitData()->BalanceId;
 if(RTSCampaignProgression::Index(Kind)<0) return;
 const int32 L=EnemyLevelForDayAndTier(Day,TierBoost);
 if(AppliedEnemyLevels.FindRef(U)==L) return;
 URTSBalanceSubsystem* B=URTSBalanceSubsystem::Get(this); if(!B) return;
 const FRTSBalanceRow* R=B->Find(Kind); if(!R) return;
 const float M=RTSCampaignProgression::Growth(L);
 const float HP=RTSCampaignProgression::BaseHealth(Kind)*M,Damage=RTSCampaignProgression::BaseDamage(Kind)*M;
 U->GetUnitData()->MaxHealth=HP;U->GetUnitData()->AttackDamage=Damage;U->GetUnitData()->HealAmount=RTSCampaignProgression::BaseHeal(Kind)*M;
 U->GetHealth()->SetMaxHealthPreservingRatio(HP);
 U->GetAttack()->SetStats(Damage,R->AttackRange,R->AttackInterval,R->AttackHitDelay);
 // Enemies wear the same appearance stages as the player, so a late push visibly looks veteran.
 if(ARTSPeasantShowcase* V=Cast<ARTSPeasantShowcase>(U)) V->RefreshTier(AppearanceForLevel(L));
 AppliedEnemyLevels.Add(U,L);
}
bool ARTSCampaign::Upgrade(FName Kind)
{
 if(GuideStep<4 || (GuideStep<7 && (Kind!=TEXT("Swordman") || SwordLevel>=2))) { Message=TEXT("先跟随向导完成当前步骤，再进行成长。"); return false; }
 if(!bReady || bTransaction || RTSCampaignProgression::Index(Kind)<0 || !IsUnitUnlocked(Kind)) return false;
 if(!CanUseService(TEXT("ResearchInstitute"))) { Message=TEXT("请先选择已建成且存活的研究所。"); return false; }
 const int32 L=GetLevel(Kind);
 if(L>=30) { Message=TEXT("本章已达30级上限。"); return false; }
 TGuardValue<bool> Guard(bTransaction,true);
 URTSResourceComponent* Stock=URTSBalanceSubsystem::Get(this)->GetResources(0);
 if(!Stock || !Stock->TrySpend(RTSCampaignProgression::UpgradeCost(Kind,L+1))) { Message=TEXT("成长资源不足：清理每日敌占资源点并采集材料。"); return false; }
 UnitLevels.Add(Kind,L+1);if(Kind==TEXT("Swordman"))SwordLevel=L+1;if(Kind==TEXT("Archer"))ArcherLevel=L+1;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) ApplyProgression(*It);
 Message=FString::Printf(TEXT("%s永久升至%d级，属性与阶段外观同步，伤势比例保留。"),*RTSCampaignProgression::Name(RTSCampaignProgression::Index(Kind)),L+1);
 return true;
}
void ARTSCampaign::CreateBuildings()
{
 auto Make=[&](FName Kind,FVector Pos)
 {
  FTransform T(Pos); ARTSBuilding* B=GetWorld()->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(B) { B->Configure(Kind,0,true); B->FinishSpawning(T); Buildings.Add(B); Owned.Add(B); }
 };
 Make(TEXT("TownHall"),Home);
}
AActor* ARTSCampaign::Marker(const FVector& Position,const FString& Label,FLinearColor Color)
{
 ARTSWorldLabel* A=GetWorld()->SpawnActor<ARTSWorldLabel>(); if(!A) { return nullptr; }
 A->Setup(Label,Color); A->SetActorLocation(Position);
 // The label is HUD-drawn: creating it must never add collision or a navigation obstacle.
 if(!A->IsVisualOnly()) { A->Destroy(); return nullptr; }
 Owned.Add(A); return A;
}
void ARTSCampaign::CreateNodes()
{
 // A fresh layout never contains the vineyard: it only appears from the day-5 clock or a saved-state rebuild.
 Vineyard.Reset(); bVineyardSpawned=false; VineyardCapture=0;
 const FVector LegacyHome(-3600,0,0);
 const FVector Positions[]={LegacyHome+FVector(950,650,0),LegacyHome+FVector(700,-1000,0),FVector(350,-950,0),FVector(6450,-3500,0),FVector(3100,2300,900),FVector(8500,4900,900),LegacyHome+FVector(-750,800,0),FVector(3600,3500,900)};
 const int32 Types[]={0,1,2,2,4,4,2,4};
 for(int32 I=0;I<8;++I)
 {
  FTransform NodeTransform(RTSCampaignMapSpace::FromLegacy(Positions[I])+FVector(0,0,90));
  ARTSResourceNode* N=GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),NodeTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!N) continue;
  if(bStagingRestore){N->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));N->SetActorEnableCollision(false);N->SetActorHiddenInGame(true);}
  N->Configure(static_cast<ERTSResource>(Types[I]),I<2?600:Types[I]==2?300:200);
  N->ConfigureResourceTier(NodeTiers[I],false);
  N->FinishSpawning(NodeTransform);
  N->SetOwningTeam(Types[I]==2?RTSTeam::Neutral:RTSTeam::Player); N->SetRenewable(true); Nodes.Add(N); Owned.Add(N);
 }
 ConfigureNodes();
}
void ARTSCampaign::ConfigureNodes()
{
 if(Nodes.Num()<8) return;
 const float Intervals[]={.35f,.35f,1.2f,.8f,2.4f,1.6f,2.4f,4.f};
 const float Regen[]={240,250,120,230,65,135,32,22};
 for(int32 I=0;I<8;++I)
 {
  const bool Iron=Nodes[I]->GetResourceType()==ERTSResource::IronOre;
  const bool Enabled=I<2 || Iron || (I==4 && (RegionMask&4)) || (I==5 && (RegionMask&4) && (FacilityMask&16)) || (I==7 && (RegionMask&4));
  if(!Enabled)Nodes[I]->SetOwningTeam(254);else if(Nodes[I]->GetOwningTeam()==254)Nodes[I]->SetOwningTeam(Iron?RTSTeam::Neutral:RTSTeam::Player);
  Nodes[I]->ConfigureEconomy(Intervals[I]/.35f,Regen[I]/60.f,I<2?600:(I==2 || I==3 || I==6)?300:200,(I==3 || I==5)?6:I>=6?2:4);
  Nodes[I]->ConfigureResourceTier(NodeTiers[I],false);
 }
 // The vineyard keeps its own saved economy and owner; this only re-applies tier/limits on unlock events.
 if(Nodes.IsValidIndex(8) && IsValid(Nodes[8].Get()))
 {
  ARTSResourceNode* V=Nodes[8].Get();
  V->SetOwningTeam(V->GetOwningTeam()==RTSTeam::Player?RTSTeam::Player:RTSTeam::Enemy);
  V->ConfigureEconomy(1.f,VineyardRecoveryPerSecond,VineyardCapacity,VineyardGatherSlots);
  V->ConfigureResourceTier(VineyardTier,true);
  Vineyard=V;
 }
}
ARTSResourceNode* ARTSCampaign::GetVineyardNode() const { return Nodes.IsValidIndex(8)?Nodes[8].Get():nullptr; }
FVector ARTSCampaign::VineyardLocation() { return VineyardSpot; }
float ARTSCampaign::GetVineyardCaptureProgress() const { return VineyardCapture/VineyardCaptureSeconds; }
void ARTSCampaign::EnsureVineyardNode(bool bForce)
{
 if(bVineyardSpawned || Nodes.Num()!=8) return;
 if(!bForce && Elapsed<VineyardSpawnElapsed) return;
 FTransform T(VineyardSpot+FVector(0,0,90));
 ARTSResourceNode* N=GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!N) { Message=TEXT("野生葡萄园生成失败，将在下一帧重试。"); return; }
 if(bStagingRestore){N->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));N->SetActorEnableCollision(false);N->SetActorHiddenInGame(true);}
 N->Configure(ERTSResource::Food,VineyardCapacity);
 N->ConfigureEconomy(1.f,VineyardRecoveryPerSecond,VineyardCapacity,VineyardGatherSlots);
 // Uncontrolled wilderness: enemy-owned so Team0 workers cannot harvest it until captured.
 N->SetOwningTeam(RTSTeam::Enemy);
 N->SetRenewable(true);
 N->ConfigureResourceTier(VineyardTier,true);
 N->FinishSpawning(T);
 Nodes.Add(N); Owned.Add(N); Vineyard=N; bVineyardSpawned=true; VineyardCapture=0;
 ClearSceneryNear(VineyardSpot);
 Message=TEXT("第5日荒野出现野生葡萄园：食物3级，容量18000、恢复8/秒；可直接采集，战斗兵驻守5秒可夺取归属。");
}
void ARTSCampaign::UpdateVineyard(float Seconds)
{
 EnsureVineyardNode();
 for(TActorIterator<ARTSResourceNode> It(GetWorld());It;++It)
 {
  // Ownership only decides the front and who guards it; harvesting stays open to every worker.
  if(It->UpdateCampaignCapture(Seconds))Message=It->GetOwningTeam()==0?TEXT("资源点已转入我方控制：前线归我方，采集对所有工人开放。"):TEXT("资源点被敌军夺取：采集不受影响，但敌军守军会攻击靠近的工人。");
 }
 if(auto* V=GetVineyardNode())VineyardCapture=V->GetCaptureProgress()*VineyardCaptureSeconds;
}
void ARTSCampaign::CreateRegions()
{
 RegionGuards.SetNum(RTSCampaignFactions::RegionCount); RegionCapture.Init(0,RTSCampaignFactions::RegionCount); RegionSpawnValid.Init(false,RTSCampaignFactions::RegionCount);
 for(int32 R=0;R<RTSCampaignFactions::RegionCount;++R) CreateRegion(R);
}
void ARTSCampaign::CreateRegion(int32 R)
{
 const int32* MeleeCount=RTSCampaignFactions::Swords; const int32* RangedCount=RTSCampaignFactions::Archers;
 const int32 Faction=RTSCampaignFactions::RegionFaction[R];
 if(R>=3)Marker(RegionCenters[R],RegionNames[R],RTSCampaignPalette::Color(Faction));
 if(RegionMask&(1<<R)) { RegionSpawnValid[R]=true; return; }
 for(int32 I=0;I<MeleeCount[R]+RangedCount[R];++I)
 {
  const bool Bow=I>=MeleeCount[R]; const int32 J=Bow?I-MeleeCount[R]:I;
  const FVector P=RegionCenters[R]+FVector((J%4)*170-250,Bow?450+(J/4)*180:(J/4)*180-200,0);
  if(ARTSUnit* U=Spawn(EnemyKindForDay(GetDay(),Bow?J:I,Bow),1,P)) { ApplyEnemyProgression(U,GetDay()); RegionGuards[R].Add(U); EncounterIds.Add(U,R); ApplyFaction(U,Faction); }
 }
 RegionSpawnValid[R]=RegionGuards[R].Num()==MeleeCount[R]+RangedCount[R];
 if(!RegionSpawnValid[R]) UE_LOG(LogTemp,Error,TEXT("Border region %d incomplete guards: %d"),R,RegionGuards[R].Num());
}
void ARTSCampaign::Start()
{
 URTSBalanceSubsystem* B=URTSBalanceSubsystem::Get(this); if(!B || !B->GetResources(0)) return;
 B->GetResources(0)->SetAmounts({{ERTSResource::Wood,500},{ERTSResource::Food,700},{ERTSResource::IronOre,0},{ERTSResource::RefinedIron,0},{ERTSResource::Amber,0}});
 CreateBuildings(); CreateNodes(); CreateRegions(); CreateDecorations();
 if(!BootstrapEnemyEconomy())
 {ClearOwned();SetActorTickEnabled(false);Message=TEXT("敌方基地地面校验失败，战役未启动；存档未改变。");return;}
 StartEconomyTrees();
 if(Nodes.Num()>0 && Nodes[0]) { UE_LOG(LogTemp,Log,TEXT("CAMPAIGN_LABEL %s"),*Nodes[0]->GetWorldLabelText().Replace(TEXT("\n"),TEXT(" | "))); }
 for(int32 I=0;I<6;++I) Spawn(TEXT("Peasant"),0,Home+FVector(450+(I%3)*160,-100+(I/3)*180,0));
 for(int32 I=0;I<6;++I) Spawn(I<4?FName("Swordman"):FName("Archer"),0,Home+FVector(1100+(I%3)*170,-300+(I/3)*200,0));
 bReady=true; FocusHome();
 if(APlayerController* PC=GetWorld()->GetFirstPlayerController()) if(URTSCampaignWidget* W=CreateWidget<URTSCampaignWidget>(PC,URTSCampaignWidget::StaticClass())) W->AddToViewport(20);
 Message=TEXT("欢迎来到边境。工人等待你的命令；先跟随左侧引导熟悉操作。");
}
void ARTSCampaign::Focus(const FVector& Position) { if(APlayerController* PC=GetWorld()->GetFirstPlayerController()) if(APawn* P=PC->GetPawn()) P->SetActorLocation(Position+FVector(0,0,100)); }
void ARTSCampaign::FocusHome() { Focus(Home); }
void ARTSCampaign::AttackObjective(int32 Index)
{
 if(Index>=0 && Index<4) { Focus(RegionCenters[Index]); Message=TEXT("已定位矿区：框选军队后右键敌人攻击；清军并靠近旗帜5秒解锁。"); }
 else if(Index==6) Focus(RegionCenters[4]);
 else if(Index==7 || Index==8) Focus(RegionCenters[Index-2]);
 else if(Index>=4 && Index<=5)
 {
  int32 Active=0; for(const auto& C:Camps) if(!C.State.Won) { if(Active++==Index-4) { Focus(C.State.Center); return; } }
  Message=TEXT("当前没有该未完成据点，日常点会随游戏日生成。");
 }
}
void ARTSCampaign::AssignWorkers(int32 Index)
{
 if(!bReady || Index<0 || Index>3) return;
 int32 NodeIndex=Index<2?Index:Index==2?((FacilityMask&4)?3:2):((FacilityMask&16)?5:4);
 if(!Nodes.IsValidIndex(NodeIndex)) { Message=TEXT("该产区尚未生成。"); return; }
 ARTSHumanController* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());
 int32 Assigned=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  ARTSUnit* U=*It; if(!U->IsAlive() || U->GetTeam()!=0 || !U->GetUnitData()->bWorker) continue;
  const bool Selected=U->IsSelected(); const bool Idle=U->GetCurrentOrder().Type==ERTSOrderType::None || U->GetCurrentOrder().Type==ERTSOrderType::Stop;
  if(!Selected && !Idle) continue;
  if(U->GetGatherer()->GetCarriedAmount()>0) continue;
  FRTSOrder O; O.Type=ERTSOrderType::Gather; O.TargetActor=Nodes[NodeIndex]; U->ReceiveOrder(O); ++Assigned;
 }
 Message=FString::Printf(TEXT("已指派%d名选中/空闲工人。携货工人请先交回；也可框选后右键改派。"),Assigned);
}

bool ARTSCampaign::CanTrain(FName Kind,FString& Error) const
{
 if(!bReady || bTransaction || GuideStep<5 || (GuideStep<7 && Kind!=TEXT("Swordman"))) { Error=TEXT("当前尚未开放该训练。"); return false; }
 const FName Service=TrainingService(Kind);
 if(Service.IsNone() || !IsUnitUnlocked(Kind)) { Error=TEXT("兵种尚未研究解锁。"); return false; }
 if(!CanUseService(Service)) { Error=TEXT("请先选择对应的完整存活训练建筑。"); return false; }
 int32 Workers=0,Soldiers=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->IsAlive() && It->GetTeam()==0) { if(It->GetUnitData()->bWorker) ++Workers; else ++Soldiers; }
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It) if(It->GetTeam()==0 && It->IsAlive()) for(FName Q:It->GetProducer()->GetQueue()) { if(Q==TEXT("Peasant")) ++Workers; else ++Soldiers; }
 const int32 Limit=GetPopulationLimit();
 if(Workers+Soldiers>=Limit)
 { Error=FString::Printf(TEXT("人口名额已用完：现有及队列共%d/%d。选工人扩建住宅，每次+8；不再单独限制工人或军队数量。"),Workers+Soldiers,Limit); return false; }
 return true;
}
bool ARTSCampaign::Train(FName Kind)
{
 if(GuideStep<5 || (GuideStep<7 && Kind!=TEXT("Swordman"))) { Message=TEXT("向导将在招募步骤开放训练。"); return false; }
 FString Error;
 if(!CanTrain(Kind,Error)) { Message=Error; return false; }
 ARTSHumanController* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());
 ARTSBuilding* B=PC?PC->GetSelectedBuilding():nullptr;
 if(B && CanUseService(TrainingService(Kind))) { const bool OK=B->GetProducer()->Enqueue(Kind,Error); Message=OK?TEXT("已付费加入训练队列，新兵继承永久等级。"):Error; return OK; }
 Message=TEXT("缺少可用训练建筑。"); return false;
}
TMap<ERTSResource,int32> ARTSCampaign::FacilityCost(int32 Index)
{
 if(Index<0 || Index>4)return {};
 const int32 Wood[]={120,220,180,400,280},Food[]={40,80,100,160,160},Iron[]={0,0,100,400,260},Amber[]={0,0,0,0,80};
 return {{ERTSResource::Wood,Wood[Index]},{ERTSResource::Food,Food[Index]},{ERTSResource::IronOre,Iron[Index]},{ERTSResource::Amber,Amber[Index]}};
}
bool ARTSCampaign::BuyFacility(int32 Index)
{
 if(GuideStep<7) { Message=TEXT("完成开局引导后开放设施建设。"); return false; }
 if(!bReady || Index<0 || Index>4 || bTransaction) return false;
 auto* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());bool bHasWorker=false;
 if(PC && PC->GetSelection())for(auto* U:PC->GetSelection()->GetSelectedUnits())if(U && U->IsAlive() && U->GetTeam()==0 && U->GetUnitData() && U->GetUnitData()->bWorker){bHasWorker=true;break;}
 if(!bHasWorker) { Message=TEXT("请先选择工人安排设施开发。"); return false; }
 if(PendingFacility>=0) { Message=TEXT("当前有设施正在建设，请稍候。"); return false; }
 if(FacilityMask&(1<<Index)) { Message=TEXT("该设施已完成，不会重复收费。"); return false; }
 if(Index>0 && !(RegionMask&(1<<(Index==4?2:Index-1)))) { Message=TEXT("请先攻下对应矿区。"); return false; }
 const TMap<ERTSResource,int32> Cost=FacilityCost(Index);
 TGuardValue<bool> Guard(bTransaction,true);
 URTSResourceComponent* S=URTSBalanceSubsystem::Get(this)->GetResources(0);
 if(!S || !S->TrySpend(Cost)) { Message=TEXT("建设资源不足。"); return false; }
 PendingFacility=Index; FacilitySeconds=FacilityBuildSeconds(Index);
 Message=FString::Printf(TEXT("已付费建设，%.0f秒后生效；非战斗时可保存剩余工期。"),FacilitySeconds); return true;
}
int32 ARTSCampaign::GetPopulationLimit() const
{
 return RTSCampaignPopulation::Limit(IsFacilityComplete(0),PopulationPurchases,LegacyPopulationBonus);
}
TMap<ERTSResource,int32> ARTSCampaign::PopulationExpansionCost() const
{
 return {{ERTSResource::Wood,RTSCampaignPopulation::WoodCost(PopulationPurchases)},{ERTSResource::Food,RTSCampaignPopulation::FoodCost(PopulationPurchases)}};
}
bool ARTSCampaign::CanBuyPopulation(FString& Error) const
{
 if(!bReady || bTransaction || bStagingRestore || GuideStep<7 || bDefeated){Error=TEXT("请完成引导，并在战役可操作时扩充人口。");return false;}
 const auto* Match=URTSMatchSubsystem::Get(this);
 if(Match && !Match->AllowsGameplay()){Error=TEXT("请先返回战场再扩充人口。");return false;}
 if(!IsFacilityComplete(0)){Error=TEXT("请先由工人完成首次住宅扩建，人口24→32；此后每次同样+8。");return false;}
 if(GetPopulationLimit()>=120){Error=TEXT("人口上限已达到120，不再收费。");return false;}
 auto* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());bool bHasWorker=false;
 if(PC && !PC->IsPlacingBuilding() && PC->GetSelection())for(auto* U:PC->GetSelection()->GetSelectedUnits())
  if(U && U->IsAlive() && U->GetTeam()==0 && U->GetUnitData() && U->GetUnitData()->bWorker){bHasWorker=true;break;}
 if(!bHasWorker){Error=TEXT("请先选择工人，再购买人口名额。");return false;}
 const auto* Balance=URTSBalanceSubsystem::Get(this);const auto* Stock=Balance?Balance->GetResources(0):nullptr;
 if(!Stock || !Stock->CanAfford(PopulationExpansionCost())){Error=TEXT("扩充人口所需木材或粮食不足。");return false;}
 return true;
}
bool ARTSCampaign::BuyPopulation()
{
 if(!CanBuyPopulation(Message))return false;
 TGuardValue<bool> Guard(bTransaction,true);
 auto* Stock=URTSBalanceSubsystem::Get(this)->GetResources(0);
 if(!Stock || !Stock->TrySpend(PopulationExpansionCost())){Message=TEXT("资源不足，人口与库存未改变。");return false;}
 ++PopulationPurchases;
 Message=FString::Printf(TEXT("已购买%d个人口名额，上限提升至%d；仅增加名额，不直接生成单位。"),RTSCampaignPopulation::PerPurchase,GetPopulationLimit());
 return true;
}
TMap<ERTSResource,int32> ARTSCampaign::CampReward(int32 Tier) const
{
 if(Tier==2) return {{ERTSResource::Wood,150},{ERTSResource::Food,180},{ERTSResource::Amber,400}};
 if(Tier==1) return {{ERTSResource::Wood,150},{ERTSResource::Food,180},{ERTSResource::IronOre,650}};
 return {{ERTSResource::Wood,500},{ERTSResource::Food,500}};
}
void ARTSCampaign::SpawnCamp(int32 Slot)
{
 int32 Active=0; for(const auto& C:Camps) if(!C.State.Won) ++Active;
 if(Active>=4) { Message=TEXT("日常据点已达4个上限，本次不额外生成。"); return; }
 FRTSCampaignCamp C; C.State.Id=SpawnDay*2+Slot; C.State.Tier=(RegionMask&4)?2:(RegionMask&1)?1:0;
 C.State.Center=RTSCampaignMapSpace::FromLegacy(FVector(-3600,Slot==0?-10500:10500,0)+FVector(FMath::Min(SpawnDay,12)*450,0,0));
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->GetTeam()==0 && FVector::Dist2D(It->GetActorLocation(),C.State.Center)<4500){ Message=TEXT("远距小据点落点靠近我方建筑，本次不生成。"); return; }
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->IsAlive() && It->GetTeam()==0 && FVector::Dist2D(It->GetActorLocation(),C.State.Center)<3500){ Message=TEXT("远距小据点落点靠近我方单位，本次不生成。"); return; }
 // Avoid overlapping an older active camp: retain its opportunity rather than stack enemies.
 for(const auto& Existing:Camps) if(!Existing.State.Won && FVector::Dist2D(Existing.State.Center,C.State.Center)<900) { Message=TEXT("同侧旧据点尚未处理，本次不叠加生成。"); return; }
 C.State.Expires=Elapsed+DaySeconds; C.State.Remaining=CampReward(C.State.Tier);
 C.Marker=Marker(C.State.Center,TEXT("每日小据点 / 清军后占领5秒"),FLinearColor(.9f,.2f,.15f));
 const int32 Melee=C.State.Tier==0?3:4;
 for(int32 I=0;I<Melee+(C.State.Tier==0?1:2);++I)
 {
  const bool Bow=I>=Melee;
  if(ARTSUnit* U=Spawn(EnemyKindForDay(GetDay(),I,Bow),1,C.State.Center+FVector((I%3)*170-170,(I/3)*180-90,0)))
  { ApplyEnemyProgression(U,GetDay(),C.State.Tier); U->SetCombatStance(ERTSCombatStance::Guard);C.Guards.Add(U); EncounterIds.Add(U,100+C.State.Id); ApplyFaction(U,Slot==0?1:0); }
 }
 if(C.Guards.Num()!=Melee+(C.State.Tier==0?1:2)) { for(auto G:C.Guards) if(G.IsValid()) G->Destroy(); if(C.Marker.IsValid()) C.Marker->Destroy(); Message=TEXT("据点位置受阻，本次未生成也不发奖。"); return; }
 Camps.Add(MoveTemp(C)); Message=TEXT("新的每日小据点出现！可定位前往；不打也不会损失已有成长。");
 ClearSceneryNear(Camps.Last().State.Center);
}
void ARTSCampaign::Award(FRTSCampaignCamp& C)
{
 URTSResourceComponent* S=URTSBalanceSubsystem::Get(this)->GetResources(0); if(!S) return;
 for(auto& P:C.State.Remaining) { const int32 Accepted=S->Deposit(P.Key,P.Value,Capacity(P.Key)); P.Value-=Accepted; }
}
void ARTSCampaign::UpdateCamps(float Seconds)
{
 for(int32 I=Camps.Num()-1;I>=0;--I)
 {
  auto& C=Camps[I];
  if(C.State.Won) { Award(C); continue; }
  bool Alive=false,Busy=false,Nearby=false;
  for(auto W:C.Guards) if(ARTSUnit* U=W.Get();U && U->IsAlive()) { Alive=true; Busy|=Fighting(U); }
  for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->IsAlive() && It->GetTeam()==0 && !It->GetUnitData()->bWorker && FVector::Dist2D(It->GetActorLocation(),C.State.Center)<650 && FMath::Abs(It->GetActorLocation().Z-C.State.Center.Z-82)<140) { Nearby=true; Busy|=Fighting(*It); }
  if(!Alive && Nearby) C.State.Capture+=Seconds; else C.State.Capture=0;
  if(C.State.Capture>=5) { C.State.Won=true; Award(C); Message=TEXT("小据点已占领，战利品已入库；满仓余量会在腾出仓位后自动领取。"); if(C.Marker.IsValid()) C.Marker->Destroy(); }
  if(!C.State.Won && Elapsed>C.State.Expires && !Busy && !Nearby)
  { for(auto W:C.Guards) if(W.IsValid()) W->Destroy(); if(C.Marker.IsValid()) C.Marker->Destroy(); Camps.RemoveAt(I); }
 }
}
void ARTSCampaign::UpdateRegions(float Seconds)
{
 for(int32 R=0;R<RTSCampaignFactions::RegionCount;++R)
 {
  if((RegionMask&(1<<R)) || !RegionSpawnValid.IsValidIndex(R) || !RegionSpawnValid[R]) continue;
  if(R>=3)
  {
   const int32 F=RTSCampaignFactions::RegionFaction[R];bool HasHall=false;
   for(ARTSBuilding* B:Buildings)if(IsValid(B) && B->IsAlive() && B->GetTeam()==1 && B->GetBuildingId()==TEXT("TownHall") && GetActorFaction(B)==F){HasHall=true;break;}
   if(HasHall)continue;
   RegionMask|=1<<R;ConfigureNodes();
   Message=FString::Printf(TEXT("%s大本营已摧毁，该势力被击败！"),RTSCampaignFactions::Name[F]);
   UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_ENEMY_HALL_DOWN faction=%d day=%d"),F,GetDay()+1);
   continue;
  }
  bool Alive=false,Nearby=false;
  for(auto W:RegionGuards[R]) if(W.IsValid() && W->IsAlive()) { Alive=true; break; }
  for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->IsAlive() && It->GetTeam()==0 && !It->GetUnitData()->bWorker && FVector::Dist2D(It->GetActorLocation(),RegionCenters[R])<700 && FMath::Abs(It->GetActorLocation().Z-RegionCenters[R].Z-82)<140) { Nearby=true; break; }
  if(!Alive && Nearby) RegionCapture[R]+=Seconds; else RegionCapture[R]=0;
  if(RegionCapture[R]>=5)
  {
   RegionMask|=1<<R; ConfigureNodes();
   Message=FString::Printf(TEXT("已攻下%s，解锁与成长永久保留。"),RegionNames[R]);
  }
 }
 if(!bCompleted && !bDefeated && (RegionMask&120)==120)
 {
  bool HasEnemyHall=false;
  for(ARTSBuilding* B:Buildings)if(IsValid(B) && B->IsAlive() && B->GetTeam()==1 && B->GetBuildingId()==TEXT("TownHall")){HasEnemyHall=true;break;}
  if(!HasEnemyHall){bCompleted=true;Message=TEXT("四方大本营全部摧毁，边境战役胜利！");UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_VICTORY halls=4 day=%d elapsed=%.1f"),GetDay()+1,Elapsed);}
 }
}
void ARTSCampaign::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!bReady)
 {
  StartupWait+=DeltaSeconds;
  UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation P;
  if(StartupWait>1 && StartupWait-DeltaSeconds<=1 && Nav) Nav->Build();
  if(StartupWait>5 && StartupWait-DeltaSeconds<=5 && Nav) UE_LOG(LogTemp,Warning,TEXT("Border startup game=%d time=%.1f building=%d project=%d"),GetWorld()->IsGameWorld(),GetWorld()->GetTimeSeconds(),Nav->IsNavigationBuildInProgress(),Nav->ProjectPointToNavigation(Home,P,FVector(200,200,500)));
  bool Ready=StartupWait>1 && Nav && Nav->ProjectPointToNavigation(Home,P,FVector(100,100,100));
  for(const FVector& Center:RegionCenters) if(Ready) Ready=Nav->ProjectPointToNavigation(Center,P,FVector(70,70,100)) && FMath::Abs(P.Location.Z-Center.Z)<60;
  if(Ready && !Nav->IsNavigationBuildInProgress()) Start();
  return;
 }
 UpdateGuide();
 if(GuideStep>=7) Elapsed+=DeltaSeconds;
 if(GuideStep>=7 && Elapsed>=AutosaveInterval && Elapsed-LastAutosaveElapsed>=AutosaveInterval)
 {
  // Automatic safe point: only when the noncombat rules allow it, never during a transaction.
  LastAutosaveElapsed=Elapsed;
  if(!bTransaction && SaveAutosave()) UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_AUTOSAVE elapsed=%.1f day=%d"),Elapsed,GetDay()+1);
 }
 if(GuideStep>=7 && GetDay()!=LastDaySnapshotDay && !bTransaction && SaveDaySnapshot())
 {
  // Day-start rollback point: keep retrying until the noncombat rules actually allow a write.
  LastDaySnapshotDay=GetDay();
  UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_DAY_SNAPSHOT day=%d elapsed=%.1f"),GetDay()+1,Elapsed);
 }
 Accumulator+=DeltaSeconds;
 if(Accumulator<.5f) return;
 const float Dt=Accumulator; Accumulator=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  if(It->GetTeam()==0 && It->IsAlive()) { if(!Owned.Contains(*It)) Owned.Add(*It); ApplyProgression(*It); }
 }
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It) if(It->GetTeam()==0 && !Owned.Contains(*It)) { Owned.Add(*It); Buildings.Add(*It);ClearSceneryNear(It->GetActorLocation(),1100); }
 // Settlement closure: losing every TownHall ends the run but never deletes the save,
 // so the player can still reload a record or keep watching surviving workers.
 if(GuideStep>=7 && !bDefeated)
 {
  bool bHasHall=false;
  for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It) if(It->GetTeam()==0 && It->IsAlive() && It->GetBuildingId()==FName("TownHall")) { bHasHall=true; break; }
  if(!bHasHall) { bDefeated=true; Message=TEXT("主城全部失守，战役判定为失败；存档未被删除，可读取记录重新开始或继续观战。"); UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_DEFEAT elapsed=%.1f day=%d"),Elapsed,GetDay()+1); }
 }
 if(PendingFacility>=0)
 {
  FacilitySeconds-=Dt;
  if(FacilitySeconds<=0)
  {
   const int32 Done=PendingFacility; FacilityMask|=1<<Done; PendingFacility=-1; ConfigureNodes();
   if(Done==1 || Done==3)
   {
    const int32 NodeIndex=Done==1?2:4; FTransform T(Nodes[NodeIndex]->GetActorLocation()+FVector(-650,0,-90));
    ARTSBuilding* B=GetWorld()->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(B) { B->Configure(TEXT("TownHall"),0,true); B->FinishSpawning(T); Buildings.Add(B); Owned.Add(B); }
   }
   Message=TEXT("设施已完成，人口或采集路线已更新。");
  }
 }
 const int32 Day=GetDay();
 if(Day!=SpawnDay) { SpawnDay=Day; SpawnMask=0; }
 const double InDay=Elapsed-Day*DaySeconds;
 if(GuideStep>=7 && !(SpawnMask&1) && InDay>=60) { SpawnMask|=1; SpawnCamp(0); }
 if(GuideStep>=7 && !(SpawnMask&2) && InDay>=210) { SpawnMask|=2; SpawnCamp(1); }
 if(GuideStep>=7) { UpdateRegions(Dt); UpdateCamps(Dt); UpdateVineyard(Dt); UpdateDaily(Dt); }
 int32 Workers=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->GetTeam()==0 && It->IsAlive())
 {
  if(It->GetUnitData()->bWorker) ++Workers;
  if(FVector::Dist2D(It->GetActorLocation(),Home)<800 && !Fighting(*It)) It->GetHealth()->ApplyHealing(It->GetHealth()->GetMaxHealth()*.02f*Dt);
 }
 if(Workers==0) { RefugeeTimer+=Dt; if(RefugeeTimer>=60) { Spawn(TEXT("Peasant"),0,Home+FVector(450,0,0)); RefugeeTimer=0; Message=TEXT("安置一名难民工人，可重新采集恢复经济。"); } } else RefugeeTimer=0;
}
FString ARTSCampaign::GetOverview() const
{
 const URTSBalanceSubsystem* B=URTSBalanceSubsystem::Get(this); const URTSResourceComponent* S=B?B->GetResources(0):nullptr;
 if(!S) return TEXT("正在准备……");
 int32 People=0; for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->GetTeam()==0 && It->IsAlive()) ++People;
 int32 Queued=0;for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->GetTeam()==0 && It->IsAlive() && It->GetProducer())Queued+=It->GetProducer()->GetQueue().Num();
 const int32 Idle=GetIdleWorkerCount();const int32 Blocked=GetBlockedProductionCount();
 FString Hints;
 if(Idle>0)Hints+=FString::Printf(TEXT(" · 空闲工人%d"),Idle);
 if(Blocked>0)Hints+=FString::Printf(TEXT(" · %d处生产等待（详情见队列）"),Blocked);
 return FString::Printf(TEXT("增量边境 · 每日5分钟\n第%d日 %02d:%02d / 05:00  人口%d＋排队%d / %d%s\n按住T选工人 · 按住G选军队（点选/框选）\n%s"),GetDay()+1,int32(FMath::Fmod(Elapsed,DaySeconds))/60,int32(FMath::Fmod(Elapsed,60)),People,Queued,GetPopulationLimit(),*Hints,*GetDailyOverview());
}
int32 ARTSCampaign::GetIdleWorkerCount() const
{
 int32 Idle=0;
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  if(!It->IsAlive() || It->GetTeam()!=0)continue;
  const URTSUnitData* Data=It->GetUnitData();if(!Data || !Data->bWorker)continue;
  const ERTSOrderType Type=It->GetCurrentOrder().Type;
  if(Type==ERTSOrderType::None || Type==ERTSOrderType::Stop)++Idle;
 }
 return Idle;
}
int32 ARTSCampaign::GetBlockedProductionCount() const
{
 int32 Blocked=0;
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)
  if(It->IsAlive() && It->GetTeam()==0 && It->GetProducer() && It->GetProducer()->IsSpawnBlocked())++Blocked;
 return Blocked;
}
FString ARTSCampaign::GetUpgradeText(FName Kind) const
{
 const int32 L=GetLevel(Kind);const float M=RTSCampaignProgression::Growth(L);const int32 I=RTSCampaignProgression::Index(Kind);
 const FString Status=L>=30?TEXT("当前满级"):CostText(RTSCampaignProgression::UpgradeCost(Kind,L+1));
 return FString::Printf(TEXT("%s Lv%d · 阶段%d\n生命%.0f 伤害%.1f 治疗%.1f\n永久升级：%s"),*RTSCampaignProgression::Name(I),L,AppearanceForLevel(L),RTSCampaignProgression::BaseHealth(Kind)*M,RTSCampaignProgression::BaseDamage(Kind)*M,RTSCampaignProgression::BaseHeal(Kind)*M,*Status);
}
FString ARTSCampaign::GetObjectives() const
{
 FString S;
 for(int32 I=0;I<RTSCampaignFactions::RegionCount;++I) S+=FString::Printf(TEXT("%s：%s\n"),RegionNames[I],(RegionMask&(1<<I))?TEXT("已攻下"):(I>=3?TEXT("摧毁敌方大本营"):TEXT("清军后驻守占领")));
 for(int32 I=0;I<Camps.Num();++I) S+=FString::Printf(TEXT("据点%d [%s] %s\n"),I+1,Camps[I].State.Tier==0?TEXT("木粮"):Camps[I].State.Tier==1?TEXT("铁矿"):TEXT("琥珀"),Camps[I].State.Won?TEXT("已领取/待领余量"):TEXT("可攻占"));
 if(ARTSResourceNode* V=GetVineyardNode()) S+=FString::Printf(TEXT("葡萄园区 [%d级食物] %d/%d · 恢复%.0f/秒 · %s\n"),V->GetResourceTier(),V->GetRemaining(),V->GetCapacity(),V->GetRecoveryPerSecond(),V->GetOwningTeam()==RTSTeam::Player?TEXT("我方控制 · 可采集"):TEXT("可直接采集 · 战斗兵驻守5秒夺取"));
 if(PendingFacility>=0) S+=FString::Printf(TEXT("设施建设剩余%.0f秒\n"),FacilitySeconds);
 return S;
}
 FName ARTSCampaign::TrainingService(FName Kind) { return RTSCampaignProgression::Service(Kind); }
 TMap<ERTSResource,int32> ARTSCampaign::GetTrainingCost(FName Kind,const AActor* Producer) const
 {
  const int32 F=GetActorFaction(Producer);
  const int32 Level=EconomyStates.IsValidIndex(F)?FMath::Max(1,EconomyStates[F].Levels.FindRef(Kind)):GetLevel(Kind);
  return RTSCampaignProgression::TrainCost(Kind,Level);
 }
 ARTSBuilding* ARTSCampaign::FindServiceBuilding(FName Kind) const
 {
  for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)
   if(It->GetTeam()==0 && It->IsAlive() && It->IsComplete() && It->GetBuildingId()==Kind) return *It;
  return nullptr;
 }
 bool ARTSCampaign::SelectServiceBuilding(FName Kind)
 {
  ARTSBuilding* B=FindServiceBuilding(Kind);
  ARTSHumanController* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());
  if(!B || !PC) { Message=TEXT("没有可用的该类建筑。"); return false; }
  PC->SelectBuilding(B); Focus(B->GetActorLocation()); return true;
 }
 FName ARTSCampaign::GetSelectedService() const
 {
  const ARTSHumanController* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());
  const ARTSBuilding* B=PC?PC->GetSelectedBuilding():nullptr;
  return IsValid(B) && B->GetTeam()==0 && B->IsAlive()?B->GetBuildingId():NAME_None;
 }
 bool ARTSCampaign::CanUseService(FName Kind) const
 {
  const ARTSHumanController* PC=Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());
  const ARTSBuilding* B=PC?PC->GetSelectedBuilding():nullptr;
  return !Kind.IsNone() && bReady && IsValid(B) && B->GetTeam()==0 && B->IsAlive() && B->IsComplete() && B->GetBuildingId()==Kind;
 }
 bool ARTSCampaign::IsUnitUnlocked(FName Kind) const
 {
  if(Kind==TEXT("Peasant") || Kind==TEXT("Swordman") || Kind==TEXT("Archer")) return true;
  const int32 Bit=ResearchBit(Kind); return Bit && (ResearchMask&Bit)!=0;
 }
 bool ARTSCampaign::ResearchUnit(FName Kind)
 {
  if(!bReady || bTransaction || GuideStep<7 || !ResearchBit(Kind)) return false;
  if(!CanUseService(TEXT("ResearchInstitute"))) { Message=TEXT("请先选择完整存活的研究所。"); return false; }
  if(IsUnitUnlocked(Kind)) { Message=TEXT("该兵种已经解锁，不会重复收费。"); return false; }
  if(GetDay()<RTSCampaignProgression::UnlockDay(Kind)){Message=TEXT("该兵种研究尚未开放。");return false;}
  TGuardValue<bool> Guard(bTransaction,true);
  URTSResourceComponent* Stock=URTSBalanceSubsystem::Get(this)->GetResources(0);
  if(!Stock || !Stock->TrySpend(ResearchCost(Kind))) { Message=TEXT("研究资源不足。"); return false; }
  ResearchMask|=ResearchBit(Kind);
  Message=TEXT("研究完成，兵种永久解锁；枪兵继承近战等级，斥候和牧师继承远程等级。"); return true;
 }
 FString ARTSCampaign::GetResearchText(FName Kind) const
 {
  if(!ResearchBit(Kind)) return TEXT("无该项研究");
  const FString Name=RTSCampaignProgression::Name(RTSCampaignProgression::Index(Kind));
  return IsUnitUnlocked(Kind)?Name+TEXT(" · 已永久解锁"):FString::Printf(TEXT("%s · %s\n%s"),*Name,GetDay()<RTSCampaignProgression::UnlockDay(Kind)?TEXT("研究尚未开放"):TEXT("研究解锁"),*CostText(ResearchCost(Kind)));
 }
void ARTSCampaign::NotifyCivilianAttacked(AActor* Victim)
{
 const auto* Unit=Cast<ARTSUnit>(Victim);const auto* Building=Cast<ARTSBuilding>(Victim);
 const bool Worker=Unit && Unit->GetTeam()==0 && Unit->GetUnitData() && Unit->GetUnitData()->bWorker;
 if(!Worker && !(Building && Building->GetTeam()==0))return;
 const double Now=GetWorld()->GetTimeSeconds();if(Now-LastCivilianWarning<2)return;LastCivilianWarning=Now;
 AlertText=Worker?TEXT("警报：我方工人遭到攻击！请保护采集线。"):TEXT("警报：我方建筑遭到攻击！请派兵支援。");AlertUntil=Now+5;Message=AlertText;
 if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Camera=Cast<ARTSCameraPawn>(PC->GetPawn()))Camera->FlashAttackWarning();
}
void ARTSCampaign::NotifyFirstIncoming(ARTSUnit* Enemy)
{
 if((SeenGuideEvents&1) || !Enemy || !Enemy->IsAlive())return;
 SeenGuideEvents|=1;Focus(Enemy->GetActorLocation());
 AlertText=TEXT("首次来袭！镜头已定位敌方部队。敌军正在出发，立即部署守军、保护工人和建筑。");AlertUntil=GetWorld()->GetTimeSeconds()+10;Message=AlertText;
}
FString ARTSCampaign::GetAlertText() const
{return GetWorld() && GetWorld()->GetTimeSeconds()<AlertUntil?AlertText:FString();}
