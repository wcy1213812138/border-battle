#pragma once
#include "Campaign/RTSCampaignProgression.h"
#include "Economy/RTSResourceComponent.h"

// Advanced Levels entries are written only by a successful research purchase.
// Existing schema13/14 saves already persist this map; absent entries stay locked.
namespace RTSCampaignEnemyProgression
{
 // Day is zero-based. These are purchase ceilings, never granted levels/stock.
 inline int32 MaxNextLevel(int32 Day){return FMath::Min(30,5+2*FMath::Clamp(Day,0,13));}
 inline int32 EffectiveResearchDay(FName Kind){return FMath::Max(0,RTSCampaignProgression::UnlockDay(Kind)-1);}
 inline int32 DesiredWorkers(int32 Day){return 4+FMath::Clamp(Day,0,2);}
 inline bool IsUnlocked(const TMap<FName,int32>& Levels,FName Kind,int32 Day)
 {
  const int32 I=RTSCampaignProgression::Index(Kind);
  return I>=0 && EffectiveResearchDay(Kind)<=Day && (I<3 || Levels.Contains(Kind));
 }
 inline bool CanSpend(const URTSResourceComponent* Account,const TMap<ERTSResource,int32>& Cost,int32 Reserve=150)
 {
  if(!Account || Cost.IsEmpty())return false;
  for(const auto& P:Cost)if(Account->GetAmount(P.Key)<P.Value+((P.Key==ERTSResource::Wood || P.Key==ERTSResource::Food)?Reserve:0))return false;
  return true;
 }
 inline FName NextResearch(const TMap<FName,int32>& Levels,FName Service,int32 Day)
 {
  // Existing economic bases have barracks and ranges, not a free support building.
  const FName Plan[]={TEXT("Spearman"),TEXT("Scout"),TEXT("Light_Infantry"),TEXT("Heavy_Infantry"),TEXT("Crossbowman"),TEXT("HeavySwordman"),TEXT("Halberdier"),TEXT("Paladin"),TEXT("Commander"),TEXT("King")};
  for(FName K:Plan)if(!Levels.Contains(K) && RTSCampaignProgression::Service(K)==Service && EffectiveResearchDay(K)<=Day)return K;
  return NAME_None;
 }
 inline bool BuyResearch(TMap<FName,int32>& Levels,URTSResourceComponent* Account,FName Kind,FName Service,int32 Day)
 {
  if(Kind.IsNone() || RTSCampaignProgression::Index(Kind)<3 || Levels.Contains(Kind) || RTSCampaignProgression::Service(Kind)!=Service || EffectiveResearchDay(Kind)>Day)return false;
  const auto Cost=RTSCampaignProgression::ResearchCost(Kind);
  if(!CanSpend(Account,Cost) || !Account->TrySpend(Cost))return false;
  Levels.Add(Kind,1);return true;
 }
 inline bool BuyUpgrade(TMap<FName,int32>& Levels,URTSResourceComponent* Account,FName Kind,int32 Day)
 {
  const int32 L=FMath::Max(1,Levels.FindRef(Kind));
  if(!IsUnlocked(Levels,Kind,Day) || L>=MaxNextLevel(Day))return false;
  const auto Cost=RTSCampaignProgression::UpgradeCost(Kind,L+1);
  if(!CanSpend(Account,Cost) || !Account->TrySpend(Cost))return false;
  Levels.Add(Kind,L+1);return true;
 }
 inline TArray<FName> Recruitment(const TMap<FName,int32>& Levels,FName Service,int32 Day,const TMap<FName,int32>& Counts)
 {
  TArray<FName> Result;
  for(int32 I=3;I<RTSCampaignProgression::Count;++I)
  {
   const FName K=RTSCampaignProgression::Kind(I);
   if(RTSCampaignProgression::Service(K)==Service && IsUnlocked(Levels,K,Day))Result.Add(K);
  }
  Result.StableSort([&](FName A,FName B){return Counts.FindRef(A)<Counts.FindRef(B);});
  const FName Basic=Service==TEXT("Barracks")?FName("Swordman"):Service==TEXT("ArcheryRange")?FName("Archer"):NAME_None;
  if(!Basic.IsNone())Result.Add(Basic);
  return Result;
 }
}
