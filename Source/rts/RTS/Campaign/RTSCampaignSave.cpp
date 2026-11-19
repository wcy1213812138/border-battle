#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignPopulation.h"
#include "Campaign/RTSCampaignEntry.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Campaign/RTSCampaignStorage.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Combat/RTSProjectile.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

namespace
{
const TCHAR* ProductionSlot=TEXT("MiniRTS_BorderFactions_v5");
const TCHAR* PreviousSlot=TEXT("MiniRTS_BorderFactions_v4");
const TCHAR* LegacySlot=TEXT("MiniRTS_BorderFactions_v3");
// Automation uses a private base slot so a test never overwrites a player's campaign save.
FString SlotOverride;
// Which manual slot of that base is currently being played; Continue follows it, and so do autosaves.
int32 ActiveSlotIndex=0;
}
/** The production name, or the automation base override. Slot suffixes are appended to this. */
FString ARTSCampaign::BaseSlotName() { return SlotOverride.IsEmpty()?FString(ProductionSlot):SlotOverride; }
FString ARTSCampaign::SaveSlotName() { return SlotNameFor(ActiveSlotIndex); }
void ARTSCampaign::SetSaveSlotOverride(const TCHAR* Slot) { SlotOverride=Slot?Slot:TEXT("");ActiveSlotIndex=0; }
int32 ARTSCampaign::ActiveSlot() { return ActiveSlotIndex; }
void ARTSCampaign::SetActiveSlot(int32 Index) { ActiveSlotIndex=FMath::Clamp(Index,0,2); }
bool ARTSCampaign::HasCampaignSave()
{
 return UGameplayStatics::DoesSaveGameExist(*SaveSlotName(),0) || UGameplayStatics::DoesSaveGameExist(*AutosaveSlotName(),0) || (SlotOverride.IsEmpty() && (UGameplayStatics::DoesSaveGameExist(PreviousSlot,0) || UGameplayStatics::DoesSaveGameExist(LegacySlot,0)));
}
bool ARTSCampaign::IsSafe(FString& Reason) const
{
 if(!bReady || bTransaction || bStagingRestore) { Reason=TEXT("场景尚未就绪或正在处理交易，请稍候。"); return false; }
 if(GuideStep<7 || !Raids.IsEmpty()) { Reason=TEXT("请先完成开局引导，并等待出征部队撤退或被消灭。"); return false; }
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It) if(It->IsAlive() && (It->GetWorld()->GetTimeSeconds()-It->GetHealth()->GetLastDamageTime()<10)) { Reason=TEXT("建筑仍在战斗或受伤不足10秒。"); return false; }
 for(TActorIterator<ARTSProjectile> It(GetWorld());It;++It) { Reason=TEXT("战场仍有飞行弹体，请脱战后稍候。"); return false; }
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  if(!It->IsAlive()) continue;
  if(It->GetAttack()->IsSwinging() || It->GetCurrentOrder().Type==ERTSOrderType::Attack || GetWorld()->GetTimeSeconds()-It->GetHealth()->GetLastDamageTime()<10) { Reason=TEXT("仍在交战或受到伤害不足10秒，请脱战后保存。"); return false; }
  if(It->IsInTrafficPass() || It->IsYieldingToTraffic()) { Reason=TEXT("单位正在短暂通行或让位，请稍候再保存。"); return false; }
 }
 return true;
}
bool ARTSCampaign::IsSafeToLoad(FString& Reason) const
{
 if(!bReady || bTransaction || bStagingRestore){Reason=TEXT("场景尚未就绪或正在处理交易，请稍候。");return false;}
 // A paused defeat may retain attackers, recent damage and projectiles forever.
 // Reads replace that battlefield atomically; all four write APIs still reject defeat.
 return bDefeated || IsSafe(Reason);
}
void ARTSCampaign::PrepareSave()
{
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->GetTeam()==0 && It->IsAlive())
 {
  FRTSOrder O; O.Type=It->GetGatherer()->GetCarriedAmount()>0?ERTSOrderType::ReturnResources:ERTSOrderType::Stop;
  It->ReceiveOrder(O);
 }
 Message=TEXT("已令军队停止、工人交货。这是可选整队操作；非战斗时可直接保存施工、训练、携货和工作任务。");
}
void ARTSCampaign::ClearOwned()
{
 // Include newly constructed buildings/units even before the campaign registration tick.
 for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)if(It->GetTeam()==0 && !It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")))Owned.AddUnique(*It);
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)if(It->GetTeam()==0 && !It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")))Owned.AddUnique(*It);
 for(AActor* A:Owned) if(IsValid(A)) { A->SetActorEnableCollision(false); A->Destroy(); }
 Owned.Reset(); Nodes.Reset(); Buildings.Reset(); Camps.Reset(); RegionGuards.Reset(); RegionCapture.Reset(); AppliedLevels.Reset(); EncounterIds.Reset();
 Factions.Reset();Raids.Reset();AppliedEnemyLevels.Reset();
 Vineyard.Reset(); bVineyardSpawned=false; VineyardCapture=0;
 DailySites.Reset();DailyGeneratedDay=-1;UnitLevels.Reset();AlertText.Empty();AlertUntil=-1000;LastCivilianWarning=-1000;LastDailyDiagnostic=-1000;
 PopulationPurchases=0;
 LegacyPopulationBonus=0;
 LastDailyGenerationAttempt=-1000;
 Decor=nullptr;DecorComponents.Reset();
 FactionAccounts.Reset();EconomyStates.Reset();EconomicFactions.Reset();EnemyNodes.Reset();EnemyNodeFactions.Reset();EconomyControllers.Reset();
}
bool ARTSCampaign::SaveCampaign()
{
 if(bDefeated){Message=TEXT("战役已失败，现场不会覆盖存档；请读取原记录重新部署。");return false;}
 if(!IsSafe(Message)) return false;
 TGuardValue<bool> Guard(bTransaction,true);
 URTSCampaignSave* S=CaptureSnapshot(Message);
 if(!S || !ValidateSave(S)) { if(S)Message=TEXT("快照自检失败，未覆盖已有存档。");return false; }
 const bool OK=RTSCampaignStorage::Write(S,SaveSlotName(),[this](const USaveGame* Candidate){return ValidateSave(Cast<URTSCampaignSave>(Candidate));},Message);
 if(OK)Message=TEXT("已保存：库存、施工、付费训练、携货与任务均已记录；有旧有效记录时已保留上一份备份。");
 return OK;
}
FString ARTSCampaign::AutosaveSlotName(){return SaveSlotName()+TEXT("_auto");}
bool ARTSCampaign::HasAutosave(){return UGameplayStatics::DoesSaveGameExist(*AutosaveSlotName(),0);}
bool ARTSCampaign::SaveAutosave()
{
 FString Reason;
 if(bDefeated || !IsSafe(Reason)) return false;
 TGuardValue<bool> Guard(bTransaction,true);
 URTSCampaignSave* S=CaptureSnapshot(Reason);
 if(!S || !ValidateSave(S)) return false;
 return RTSCampaignStorage::Write(S,AutosaveSlotName(),[this](const USaveGame* Candidate){return ValidateSave(Cast<URTSCampaignSave>(Candidate));},Reason);
}
bool ARTSCampaign::LoadAutosave(){return LoadCampaignInternal(false,false,true);}
/**
 * Day-start snapshot: written automatically when the in-game day rolls over, so "reset today"
 * always returns to the same point the day began at instead of a hand-made save.
 */
bool ARTSCampaign::SaveDaySnapshot()
{
 FString Reason;
 if(bDefeated || !IsSafe(Reason)) return false;
 TGuardValue<bool> Guard(bTransaction,true);
 URTSCampaignSave* S=CaptureSnapshot(Reason);
 if(!S || !ValidateSave(S)) return false;
 return RTSCampaignStorage::Write(S,DaySnapshotSlotName(),[this](const USaveGame* Candidate){return ValidateSave(Cast<URTSCampaignSave>(Candidate));},Reason);
}
bool ARTSCampaign::HasDaySnapshot() const
{
	// A record from another day (or from before a load) must never pass as "today's start".
	if(!UGameplayStatics::DoesSaveGameExist(*DaySnapshotSlotName(),0))return false;
	const URTSCampaignSave* S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(DaySnapshotSlotName()));
	if(!S || S->Version<3 || S->Version>URTSCampaignSave::SnapshotVersion || !FMath::IsFinite(S->Elapsed))return false;
	return FMath::FloorToInt(S->Elapsed/DaySeconds)==GetDay();
}
bool ARTSCampaign::LoadDaySnapshot()
{
 if(!IsSafeToLoad(Message)) return false;
 if(!HasDaySnapshot()) { Message=TEXT("本日还没有开局快照：换日起且脱战时会自动记录。"); return false; }
 return LoadSnapshotFromSlot(DaySnapshotSlotName(),false);
}
FString ARTSCampaign::SlotNameFor(int32 Index)
{
 // Slot 0 is the primary manual slot; 1-2 map to fixed suffixes so autosave, day and backups stay separate.
 const FString Base=BaseSlotName();
 return Index<=0?Base:Base+FString::Printf(TEXT("_alt%d"),Index+1);
}
bool ARTSCampaign::HasSlot(int32 Index){return UGameplayStatics::DoesSaveGameExist(*SlotNameFor(Index),0);}
bool ARTSCampaign::SaveToSlot(int32 Index)
{
 if(bDefeated){Message=TEXT("战役已失败，现场不会覆盖存档；请读取原记录重新部署。");return false;}
 // Always writes the slot the player picked, even when a different slot is currently active.
 FString Reason;
 if(!IsSafe(Reason)) { Message=Reason; return false; }
 TGuardValue<bool> Guard(bTransaction,true);
 URTSCampaignSave* S=CaptureSnapshot(Reason);
 if(!S) { Message=Reason.IsEmpty()?TEXT("无法生成快照，未写入存档。"):Reason;UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_SAVE_CAPTURE_REJECT %s"),*Message);return false; }
 if(!ValidateSave(S)) { Message=TEXT("快照自检失败，未覆盖已有存档。");UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_SAVE_VALIDATE_REJECT units=%d buildings=%d day=%d elapsed=%.2f"),S->Units.Num(),S->BuildingKinds.Num(),GetDay()+1,Elapsed);return false; }
 const bool OK=RTSCampaignStorage::Write(S,SlotNameFor(Index),[this](const USaveGame* Candidate){return ValidateSave(Cast<URTSCampaignSave>(Candidate));},Reason);
 Message=OK?FString::Printf(TEXT("已保存到槽 %d；该槽现在是当前存档（继续与自动档都跟着它）。"),Index+1):Reason;
 return OK;
}
bool ARTSCampaign::LoadFromSlot(int32 Index)
{
 if(!IsSafeToLoad(Message)) return false;
 // Extra slots never fall back to another slot: the player asked for exactly this record.
 return LoadSnapshotFromSlot(SlotNameFor(Index),false);
}
bool ARTSCampaign::DescribeSlot(int32 Index,FRTSSlotInfo& OutInfo)
{
	OutInfo=FRTSSlotInfo();
	bool bExists=false;int64 Size=0;FDateTime Modified;
	if(!RTSCampaignStorage::Describe(SlotNameFor(Index),bExists,Size,Modified))return false;
	OutInfo.bExists=bExists;OutInfo.Size=FMath::Max<int64>(0,Size);OutInfo.Modified=Modified;
	return true;
}
bool ARTSCampaign::DeleteSlotRecords(int32 Index,FString& OutSummary)
{
	OutSummary.Reset();
	TArray<FString> Slots;
	Slots.Add(SlotNameFor(Index));
	Slots.Add(RTSCampaignStorage::PreviousSlot(SlotNameFor(Index)));
	// The day-start rollback point belongs to this slot too; leaving it would let "reset today" resurrect a deleted run.
	Slots.Add(SlotNameFor(Index)+TEXT("_day"));
	if(Index<=0)
	{
		// The autosave belongs to the main chain: cleaning the main record must not leave a hidden fallback.
		Slots.Add(AutosaveSlotName());
		Slots.Add(RTSCampaignStorage::PreviousSlot(AutosaveSlotName()));
	}
	if(Index<=0 && SlotOverride.IsEmpty())
	{
		// Production installs may also hold the older v4/v3 fallbacks; cleaning the main record clears them.
		Slots.Add(PreviousSlot);
		Slots.Add(LegacySlot);
	}
	int32 Deleted=0;
	for(const FString& Slot:Slots)
	{
		bool bExists=false;int64 Size=0;FDateTime Modified;
		RTSCampaignStorage::Describe(Slot,bExists,Size,Modified);
		if(!bExists)continue;
		FString Error;
		if(!RTSCampaignStorage::Delete(Slot,Error))
		{ OutSummary=FString::Printf(TEXT("清理槽 %d 时失败：%s（已删除 %d 个文件）。"),Index+1,*Error,Deleted); return false; }
		++Deleted;
	}
	OutSummary=FString::Printf(TEXT("槽 %d 已清理，共删除 %d 个文件。"),Index+1,Deleted);
	return Deleted>0;
}
bool ARTSCampaign::ValidateSave(const URTSCampaignSave* S) const
{
 if(!S || S->Version<3 || S->Version>URTSCampaignSave::SnapshotVersion || S->ResearchMask<0 || S->ResearchMask>(S->Version<8?7:8191) || (S->Version==3 && S->ResearchMask!=0) || S->GuideStep!=7 || S->RaidMask<0 || S->RaidMask>(S->Version<5?3:RTSCampaignFactions::RaidMask) || S->RaidDay!=FMath::FloorToInt(S->Elapsed/(S->Version<8?1200.f:DaySeconds)) || S->CampaignId.IsEmpty() || !FMath::IsFinite(S->Elapsed) || S->Elapsed<0 || S->Elapsed>1e7 || S->SwordLevel<1 || S->SwordLevel>30 || S->ArcherLevel<1 || S->ArcherLevel>30 || S->RegionMask<0 || S->RegionMask>(S->Version<5?31:RTSCampaignFactions::RegionMask) || S->FacilityMask<0 || S->FacilityMask>31 || S->SpawnMask<0 || S->SpawnMask>3 || S->SpawnDay!=FMath::FloorToInt(S->Elapsed/(S->Version<8?1200.f:DaySeconds))) return false;
 if(S->SeenGuideEvents<0 || S->SeenGuideEvents>7) return false;
 if(!RTSCampaignPopulation::IsValid(S->PopulationPurchases) || (S->Version<15 && S->PopulationPurchases>7) || (S->PopulationPurchases>0 && (S->Version<14 || !(S->FacilityMask&1))))return false;
 if((S->LegacyPopulationBonus!=0 && S->LegacyPopulationBonus!=32) || (S->LegacyPopulationBonus>0 && (S->Version<15 || !(S->FacilityMask&1))) || ((S->FacilityMask&1) && 32+S->PopulationPurchases*8+S->LegacyPopulationBonus>120))return false;
 if(S->Units.Num()>200 || S->Camps.Num()>1000 || (S->NodeRemaining.Num()!=8 && !(S->Version>=7 && S->NodeRemaining.Num()==9)) || S->NodeRemainders.Num()!=S->NodeRemaining.Num() || (S->NodeTiers.Num()!=0 && S->NodeTiers.Num()!=S->NodeRemaining.Num()) || (S->NodeOwners.Num()!=0 && S->NodeOwners.Num()!=S->NodeRemaining.Num()) || S->BuildingKinds.Num()!=S->BuildingPositions.Num() || S->BuildingKinds.Num()!=S->BuildingHealth.Num() || S->BuildingKinds.Num()>30) return false;
 if(S->RegionSpawnValid.Num()!=(S->Version<5?5:RTSCampaignFactions::RegionCount)) return false;
 for(const auto& P:S->Stock) if(P.Key>=ERTSResource::Count || P.Value<0 || P.Value>10000000) return false;
 for(int32 I=0;I<S->NodeRemaining.Num();++I)
 {
  const int32 Limit=I==8?VineyardCapacity:600;
  if(S->NodeRemaining[I]<0 || S->NodeRemaining[I]>Limit || !FMath::IsFinite(S->NodeRemainders[I]) || S->NodeRemainders[I]<0 || S->NodeRemainders[I]>=1) return false;
  if(S->NodeTiers.IsValidIndex(I) && (S->NodeTiers[I]<1 || S->NodeTiers[I]>3)) return false;
  if(I==8 && (!S->NodeTiers.IsValidIndex(I) || S->NodeTiers[I]!=VineyardTier)) return false;
  if(S->NodeOwners.IsValidIndex(I))
  {
   const uint8 NodeOwner=S->NodeOwners[I];
   if(NodeOwner!=RTSTeam::Player && NodeOwner!=RTSTeam::Enemy && NodeOwner!=RTSTeam::Neutral && NodeOwner!=254) return false;
   if(I==8 && NodeOwner!=RTSTeam::Player && NodeOwner!=RTSTeam::Enemy) return false;
  }
 }
 auto ValidPosition=[S](const FVector& V) { return (S->Version<11?RTSCampaignMapSpace::ContainsLegacyXY(V):RTSCampaignMapSpace::ContainsXY(V)) && FMath::IsFinite(V.Z) && V.Z>-300 && V.Z<1800; };
 TSet<int32> DailyIds;
 if(S->DailySites.Num()>32 || S->DailyGeneratedDay<-1 || S->DailyGeneratedDay>FMath::FloorToInt(S->Elapsed/DaySeconds))return false;
 for(const auto& P:S->UnitLevels)if(RTSCampaignProgression::Index(P.Key)<0 || P.Value<1 || P.Value>30)return false;
 for(const auto& Site:S->DailySites)
 {
  if(Site.Id<0 || Site.Id>=10000 || DailyIds.Contains(Site.Id) || !ValidPosition(Site.Position) || Site.Type>=ERTSResource::Count || Site.Tier<1 || Site.Tier>3 || Site.Day<0 || Site.Day>100000 || (Site.Owner!=0 && Site.Owner!=1 && Site.Owner!=255) || Site.Remaining<0 || Site.Remaining>100000 || !FMath::IsFinite(Site.RecoveryRemainder) || Site.RecoveryRemainder<0 || Site.RecoveryRemainder>=1 || !FMath::IsFinite(Site.Capture) || Site.Capture<0 || Site.Capture>5.5f || Site.Pulses<0 || Site.Pulses>2 || !FMath::IsFinite(Site.NextPush))return false;
  DailyIds.Add(Site.Id);
 }
 TSet<int32> Ids;
 for(const auto& C:S->Camps)
 {
  if(C.Id<0 || Ids.Contains(C.Id) || C.Tier<0 || C.Tier>2 || !ValidPosition(C.Center) || !FMath::IsFinite(C.Expires) || C.Expires<0 || !FMath::IsFinite(C.Capture) || C.Capture<0 || C.Capture>5.5f) return false;
  Ids.Add(C.Id); const auto Reward=CampReward(C.Tier);
  for(const auto& P:C.Remaining) if(P.Value<0 || P.Value>Reward.FindRef(P.Key) || P.Key>=ERTSResource::Count) return false;
 }
 for(const auto& U:S->Units)
 {
  const int32 KindIndex=RTSCampaignProgression::Index(U.Kind);const int32 Bit=KindIndex>=3?1<<(KindIndex-3):0;
  if(KindIndex<0 || (Bit && (S->Version<4 || (U.Team==0 && !(S->ResearchMask&Bit)))) || U.Team>1 || !ValidPosition(U.Feet) || !FMath::IsFinite(U.HealthFraction) || U.HealthFraction<=0 || U.HealthFraction>1 || !FMath::IsFinite(U.MaxHealth) || U.MaxHealth<=0 || U.MaxHealth>100000 || !FMath::IsFinite(U.Damage) || U.Damage<0 || U.Damage>10000) return false;
  if(U.Faction<0 || U.Faction>=(S->Version<5?2:RTSCampaignFactions::FactionCount)) return false;
  const int32 Regions=S->Version<5?5:RTSCampaignFactions::RegionCount;
  const bool DailyEncounter=U.Encounter>=10000 && U.Encounter<30000 && DailyIds.Contains(U.Encounter-(U.Encounter>=20000?20000:10000));
  const bool EconomyEncounter=S->Version>=13 && U.Encounter==30000+U.Faction;
  if(U.Team==1 && !DailyEncounter && !EconomyEncounter && (U.Encounter<0 || (U.Encounter>=Regions && (U.Encounter<100 || !Ids.Contains(U.Encounter-100))))) return false;
  if(U.Team==1 && U.Encounter<Regions && (U.Faction!=RTSCampaignFactions::RegionFaction[U.Encounter] || (U.Encounter<3 && (S->RegionMask&(1<<U.Encounter))))) return false;
 }
 for(int32 I=0;I<S->BuildingKinds.Num();++I)
 {
  const FName Kind=S->BuildingKinds[I];
  const bool NewKind=Kind==TEXT("ArcheryRange") || Kind==TEXT("ResearchInstitute") || Kind==TEXT("SupportBarracks");
  if((Kind!=TEXT("TownHall") && Kind!=TEXT("Barracks") && Kind!=TEXT("Farm") && Kind!=TEXT("Tower") && !NewKind) || (NewKind && S->Version<4) || !URTSBalanceSubsystem::Get(this)->Find(Kind) || !ValidPosition(S->BuildingPositions[I]) || !FMath::IsFinite(S->BuildingHealth[I]) || S->BuildingHealth[I]<=0 || S->BuildingHealth[I]>1) return false;
 }
 if(S->Version>=13)
 {
  if(S->FactionEconomies.Num()!=4 || S->BuildingTeams.Num()!=S->BuildingKinds.Num() || S->BuildingFactions.Num()!=S->BuildingKinds.Num() || S->EnemyNodes.Num()>4*static_cast<int32>(ERTSResource::Count))return false;
  for(const auto& E:S->FactionEconomies){if(E.Stock.Num()>static_cast<int32>(ERTSResource::Count) || E.Levels.Num()>16 || !FMath::IsFinite(E.NextRaid) || E.NextRaid<0 || E.NextRaid>1e7+1000 || !FMath::IsFinite(E.RetreatAt) || E.RetreatAt<0 || E.RetreatAt>1e7+1000)return false;for(const auto& P:E.Stock)if(P.Key>=ERTSResource::Count || P.Value<0 || P.Value>10000000)return false;for(const auto& P:E.Levels)if(RTSCampaignProgression::Index(P.Key)<0 || P.Value<1 || P.Value>30)return false;}
  TSet<int32> NodeKeys;
  for(const auto& N:S->EnemyNodes){const int32 Key=N.Faction*static_cast<int32>(ERTSResource::Count)+static_cast<int32>(N.Type);if(N.Faction<0 || N.Faction>=4 || N.Type>=ERTSResource::Count || NodeKeys.Contains(Key) || !ValidPosition(N.Feet) || FVector::Dist2D(N.Feet,RTSCampaignFactions::Base[N.Faction])>6000 || N.Remaining<0 || N.Remaining>6000 || !FMath::IsFinite(N.Remainder) || N.Remainder<0 || N.Remainder>=1 || (N.Owner!=0 && N.Owner!=1 && N.Owner!=255) || !ARTSResourceNode::IsValidOwnership(N.Capture,N.Owner))return false;NodeKeys.Add(Key);}
  for(int32 I=0;I<S->BuildingTeams.Num();++I)if(S->BuildingTeams[I]>1 || (S->BuildingTeams[I]==0?S->BuildingFactions[I]!=-1:(S->BuildingFactions[I]<0 || S->BuildingFactions[I]>=4)))return false;
 }
 if(S->Version>=12)
 {
  if(S->BuildingWork.Num()!=S->BuildingKinds.Num() || S->PendingFacility<-1 || S->PendingFacility>4 || !FMath::IsFinite(S->FacilitySeconds) || (S->PendingFacility<0?S->FacilitySeconds!=0:(S->FacilitySeconds<=0 || S->FacilitySeconds>(S->PendingFacility==0?10:20) || (S->FacilityMask&(1<<S->PendingFacility)))) || !FMath::IsFinite(S->SimulationRemainder) || S->SimulationRemainder<0 || S->SimulationRemainder>1 || !FMath::IsFinite(S->RefugeeSeconds) || S->RefugeeSeconds<0 || S->RefugeeSeconds>60 || S->RaidWaves.Num()!=RTSCampaignFactions::FactionCount || S->RegionCapture.Num()!=RTSCampaignFactions::RegionCount)return false;
  if(S->NodeCapture.Num()!=S->NodeRemaining.Num() || S->DailyCapture.Num()!=S->DailySites.Num() || S->NodeOwners.Num()!=S->NodeRemaining.Num() || S->NodeTiers.Num()!=S->NodeRemaining.Num())return false;
  for(int32 I=0;I<S->NodeCapture.Num();++I)if(!ARTSResourceNode::IsValidOwnership(S->NodeCapture[I],S->NodeOwners[I]))return false;
  for(int32 I=0;I<S->DailyCapture.Num();++I)if(!ARTSResourceNode::IsValidOwnership(S->DailyCapture[I],S->DailySites[I].Owner))return false;
  for(int32 Waves:S->RaidWaves)if(Waves<0 || Waves>3)return false;
  for(float Progress:S->RegionCapture)if(!FMath::IsFinite(Progress) || Progress<0 || Progress>5.5f)return false;
  for(int32 I=0;I<S->BuildingWork.Num();++I)
  {
   const auto& Work=S->BuildingWork[I];
   if(!FMath::IsFinite(Work.Progress) || Work.Progress<0 || Work.Progress>1 || !FMath::IsFinite(Work.Facing) || FMath::Abs(Work.Facing)>360 || !URTSProducerComponent::IsValidProduction(Work.Production,S->BuildingKinds[I],Work.Progress>=1,true))return false;
   if(Work.bHasFoodSource && (S->BuildingKinds[I]!=TEXT("Farm") || Work.Progress<1 || !ValidPosition(Work.FoodFeet) || FVector::Dist2D(Work.FoodFeet,S->BuildingPositions[I])>2000 || Work.FoodRemaining<0 || Work.FoodRemaining>300 || !FMath::IsFinite(Work.FoodRemainder) || Work.FoodRemainder<0 || Work.FoodRemainder>=1))return false;
   for(const auto& Paid:Work.Production.Orders)
   {
    const int32 Kind=RTSCampaignProgression::Index(Paid.Kind);
    if(Kind<0 || !URTSBalanceSubsystem::Get(this)->Find(Paid.Kind))return false;
    if(Kind>=3)
    {
     if(S->Version>=13 && S->BuildingTeams[I]==1)
     {const int32 F=S->BuildingFactions[I];if(!S->FactionEconomies.IsValidIndex(F) || S->FactionEconomies[F].Levels.FindRef(Paid.Kind)<1)return false;}
     else if(!(S->ResearchMask&(1<<(Kind-3))))return false;
    }
   }
  }
  TMap<int32,FVector> GroupGoals;
  for(const auto& U:S->Units)
  {
   const bool Worker=U.Kind==TEXT("Peasant");
   if(static_cast<uint8>(U.Stance)>static_cast<uint8>(ERTSCombatStance::Aggressive) || !FMath::IsFinite(U.Facing) || FMath::Abs(U.Facing)>360 || !FMath::IsFinite(U.CollisionRadius) || U.CollisionRadius<28 || U.CollisionRadius>60 || U.OrderType>ERTSOrderType::ReturnResources || U.OrderType==ERTSOrderType::Attack || U.TargetClass>(S->Version>=13?6:5) || U.TargetIndex<-1 || U.MoveGroup<-1 || U.MoveGroup>=200 || !URTSGathererComponent::IsValidEconomy(U.Economy,Worker?10:0))return false;
   if(!Worker && (U.Economy.bReturning || U.Economy.WorkElapsed!=0))return false;
   const bool Fixed=U.TargetClass==1 && S->NodeRemaining.IsValidIndex(U.TargetIndex);
   const bool Daily=U.TargetClass==2 && DailyIds.Contains(U.TargetIndex);
   const bool Building=U.TargetClass==3 && S->BuildingKinds.IsValidIndex(U.TargetIndex);
   const bool Unit=U.TargetClass==4 && S->Units.IsValidIndex(U.TargetIndex);
   const bool Farm=U.TargetClass==5 && S->BuildingWork.IsValidIndex(U.TargetIndex) && S->BuildingWork[U.TargetIndex].bHasFoodSource;
   const bool Local=S->Version>=13 && U.TargetClass==6 && S->EnemyNodes.IsValidIndex(U.TargetIndex);
   if(Local && U.Team==1 && (U.Encounter!=30000+U.Faction || S->EnemyNodes[U.TargetIndex].Faction!=U.Faction))return false;
   if(S->Version>=13 && Building && (U.Team!=S->BuildingTeams[U.TargetIndex] || (U.Team==1 && U.Faction!=S->BuildingFactions[U.TargetIndex])))return false;
   if(U.TargetClass==0?U.TargetIndex!=-1:!(Fixed || Daily || Building || Unit || Farm || Local))return false;
   if(U.OrderType==ERTSOrderType::Gather && (!Worker || !(Fixed || Daily || Farm || Local)))return false;
   if(U.OrderType==ERTSOrderType::Build && (!Worker || !Building || S->BuildingWork[U.TargetIndex].Progress>=1))return false;
   if(U.OrderType==ERTSOrderType::ReturnResources && (!Worker || (U.TargetClass!=0 && (!Building || S->BuildingWork[U.TargetIndex].Progress<1 || !URTSBalanceSubsystem::Get(this)->Find(S->BuildingKinds[U.TargetIndex])->bDropOff))))return false;
   if((U.OrderType==ERTSOrderType::None || U.OrderType==ERTSOrderType::Stop || U.OrderType==ERTSOrderType::Move) && U.TargetClass!=0)return false;
   if(U.MoveGroup>=0)
   {
    if(U.OrderType!=ERTSOrderType::Move || !ValidPosition(U.MoveGoal))return false;
    if(const FVector* Goal=GroupGoals.Find(U.MoveGroup)){if(!Goal->Equals(U.MoveGoal,.01))return false;}else GroupGoals.Add(U.MoveGroup,U.MoveGoal);
   }
   if(U.OrderType==ERTSOrderType::Move && !ValidPosition(U.OrderLocation))return false;
  }
 }
 return true;
}
bool ARTSCampaign::LoadCampaign() { return LoadCampaignInternal(false); }
bool ARTSCampaign::HasPreviousCampaign(){return UGameplayStatics::DoesSaveGameExist(*RTSCampaignStorage::PreviousSlot(SaveSlotName()),0);}
bool ARTSCampaign::LoadPreviousCampaign(){return LoadCampaignInternal(false,true);}
bool ARTSCampaign::LoadCampaignOnEntry()
{
 if(bEntryLoadConsumed || !bReady || GuideStep!=0 || Elapsed!=0 || !RTSCampaignEntry::IsContinueRequest(this))return false;
 bEntryLoadConsumed=true;
 return LoadCampaignInternal(true,GetWorld()->URL.HasOption(TEXT("CampaignPrevious")));
}
bool ARTSCampaign::LoadCampaignInternal(bool bFromEntry,bool bPrevious,bool bAutosave)
{
 if(!bReady || bTransaction || bStagingRestore || (!bFromEntry && !IsSafeToLoad(Message))) return false;
 const FString RequestedSlot=bAutosave?AutosaveSlotName():(bPrevious?RTSCampaignStorage::PreviousSlot(SaveSlotName()):SaveSlotName());
 // Menu Continue reads exactly the selected slot; never substitute an older record.
 return LoadSnapshotFromSlot(RequestedSlot,!bFromEntry && !bPrevious && !bAutosave);
}
bool ARTSCampaign::LoadSnapshotFromSlot(const FString& RequestedSlot,bool bAllowFallback)
{
 URTSCampaignSave* S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(RequestedSlot));
 // Fall back only if the primary is absent; recovery backup always needs explicit confirmation.
 if(bAllowFallback && !UGameplayStatics::DoesSaveGameExist(*RequestedSlot,0))
 {
  const TCHAR* Fallback=SlotOverride.IsEmpty()?(UGameplayStatics::DoesSaveGameExist(PreviousSlot,0)?PreviousSlot:LegacySlot):nullptr;
  if(!Fallback)
  {
   // A first-time chapter may only have the automatic safe point.
   S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(AutosaveSlotName()));
   if(!S){ Message=TEXT("该槽位没有战役记录。");return false; }
  }
  else S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(Fallback));
 }
 if(!ValidateSave(S)) { Message=TEXT("无有效存档或快照校验失败；当前战役未改变。"); return false; }
 const int32 OriginalVersion=S->Version;
 if(OriginalVersion<5)
 {
  // Encounter IDs remain stable. Move only surviving main-base guards, never amber nodes,
  // outposts, player units or daily camps; retain health and local formation offsets.
  const FVector OldBases[]={FVector(7900,4800,900),FVector(11200,-4700,0)};
  for(auto& U:S->Units) if(U.Team==1 && (U.Encounter==3 || U.Encounter==4))
   U.Feet+=RTSCampaignFactions::LegacyBase[U.Encounter-3]-OldBases[U.Encounter-3];
  S->RegionSpawnValid.SetNum(RTSCampaignFactions::RegionCount);
  for(int32 R=5;R<RTSCampaignFactions::RegionCount;++R)
  {
   S->RegionSpawnValid[R]=true;
   const int32 Melee=RTSCampaignFactions::Swords[R], Bows=RTSCampaignFactions::Archers[R];
   for(int32 I=0;I<Melee+Bows;++I)
   {
    const bool Bow=I>=Melee; const int32 J=Bow?I-Melee:I;
    FRTSCampaignUnitSave U; U.Team=1; U.Faction=RTSCampaignFactions::RegionFaction[R]; U.Encounter=R;
    U.Kind=Bow?FName("Archer"):FName("Swordman");
    U.Feet=RTSCampaignFactions::LegacyRegionCenters[R]+FVector((J%4)*170-250,Bow?450+(J/4)*180:(J/4)*180-200,0);
    U.MaxHealth=R==5?(Bow?260:520):(Bow?280:480); U.Damage=R==5?(Bow?34:46):(Bow?36:42);
    S->Units.Add(U);
   }
  }
  // Avoid retroactively launching the newly introduced factions on the migration day.
  S->RaidMask|=12; S->Completed=false; S->Version=5;
  if(!ValidateSave(S)) { Message=TEXT("旧存档迁移校验失败（含单位上限200）；原存档和当前战役未改变。"); return false; }
 }
 if(OriginalVersion<6)
 {
  const FVector OldOutposts[]={FVector(-250,-1500,0),FVector(5800,-3500,0),FVector(2500,2200,900)};
  for(auto& U:S->Units) if(U.Team==1 && U.Encounter>=0 && U.Encounter<3)
   U.Feet+=RTSCampaignFactions::LegacyRegionCenters[U.Encounter]-OldOutposts[U.Encounter];
  S->Version=6;
  if(!ValidateSave(S)) { Message=TEXT("前哨布局迁移校验失败；当前战役未改变。"); return false; }
 }
 if(OriginalVersion<7)
 {
  // Version 7 only adds node grading/ownership arrays. Keep the eight saved slots untouched:
  // no retroactive vineyard and no offline catch-up, so an old save never gains free resources.
  S->NodeTiers.Reset(); S->NodeOwners.Reset();
  S->Version=7;
  if(!ValidateSave(S)) { Message=TEXT("资源节点分级迁移校验失败；当前战役未改变。"); return false; }
 }
 if(OriginalVersion<8)
 {
  S->Elapsed*=DaySeconds/1200.f;for(auto& Camp:S->Camps)Camp.Expires*=DaySeconds/1200.f;
  for(int32 I=0;I<RTSCampaignProgression::Count;++I){const FName K=RTSCampaignProgression::Kind(I);S->UnitLevels.Add(K,K==TEXT("Swordman") || K==TEXT("Spearman")?S->SwordLevel:K==TEXT("Archer") || K==TEXT("Scout") || K==TEXT("Priest")?S->ArcherLevel:1);}
  S->DailyGeneratedDay=-1;S->DailySites.Reset();S->Version=8;
  if(!ValidateSave(S)){Message=TEXT("五分钟日循环迁移校验失败，当前战役未改变。");return false;}
 }
 if(OriginalVersion<10)
 {
  // Older southwest terrain snapshots retain their existing height; add only the delta.
  auto Lift=[OriginalVersion](FVector& P){const double H=RTSCampaignTerrain::LegacyHeightAt(P.X,P.Y);const double Old=OriginalVersion>=9?RTSCampaignTerrain::LegacySouthwestHeight(P.X,P.Y):0.0;if(H>Old && FMath::Abs(P.Z-Old)<100)P.Z+=H-Old;};
  for(auto& U:S->Units)Lift(U.Feet);
  for(auto& P:S->BuildingPositions)Lift(P);
  for(auto& Camp:S->Camps)Lift(Camp.Center);
  for(auto& Site:S->DailySites)Lift(Site.Position);
  S->Version=10;
  if(!ValidateSave(S)){Message=TEXT("山地高度迁移校验失败，当前战役未改变。");return false;}
 }
 // Transform exactly once in memory. Loading never writes the original production slot.
 if(!MigrateCompactLayout(S) || !ValidateSave(S)){Message=TEXT("紧凑地图坐标迁移校验失败，原存档和当前战役未改变。");return false;}
 if(OriginalVersion==3)
 {
  const FName Kinds[]={TEXT("ArcheryRange"),TEXT("ResearchInstitute"),TEXT("SupportBarracks")};
  const FVector Positions[]={FVector(-4250,-650,0),FVector(-4250,0,0),FVector(-4900,-650,0)};
  for(int32 I=0;I<3;++I){S->BuildingKinds.Add(Kinds[I]);S->BuildingPositions.Add(RTSCampaignMapSpace::FromLegacy(Positions[I]));S->BuildingHealth.Add(1);}
  if(!ValidateSave(S)){Message=TEXT("服务建筑迁移失败，当前战役未改变。");return false;}
 }
 if(!PreflightSnapshot(S,Message))return false;
 TGuardValue<bool> Guard(bTransaction,true);
 if(!RestoreSnapshot(S,Message))return false;
 // The restored run is already past its own day start, so pin the current day to avoid
 // immediately overwriting the day-start record with a mid-day state.
 LastDaySnapshotDay=GetDay();
 Message=TEXT("战役已完整恢复：施工、付费队列、携货与任务继续；资源和奖励不重复发放。");
 FocusHome();return true;
}

bool ARTSCampaign::MigrateCompactLayout(URTSCampaignSave* S)
{
 if(!S || S->Version<10 || S->Version>URTSCampaignSave::SnapshotVersion)return false;
 if(S->Version>=11)return true;
 auto Convert=[](FVector& P)
 {
  // Historical default-valued Version fields can be absent. Repair only positions still
  // on an older terrain surface; never lift already elevated units or rewrite their file.
  const double H=RTSCampaignTerrain::LegacyHeightAt(P.X,P.Y);
  const double Old=RTSCampaignTerrain::LegacySouthwestHeight(P.X,P.Y);
  if(H>100 && FMath::Abs(P.Z)<100)P.Z+=H;
  else if(H>Old+100 && FMath::Abs(P.Z-Old)<100)P.Z+=H-Old;
  P=RTSCampaignMapSpace::FromLegacy(P);
 };
 for(auto& U:S->Units)Convert(U.Feet);
 for(auto& P:S->BuildingPositions)Convert(P);
 for(auto& C:S->Camps)Convert(C.Center);
 for(auto& Site:S->DailySites)Convert(Site.Position);
 S->Version=11;
 return true;
}
