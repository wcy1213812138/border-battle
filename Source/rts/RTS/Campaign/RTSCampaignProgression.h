#pragma once
#include "CoreMinimal.h"
#include "Core/RTSTypes.h"
namespace RTSCampaignProgression
{
 inline constexpr int32 Count=16;
 struct FUnit { const TCHAR* Id;const TCHAR* Label;const TCHAR* Building;float HP,Damage,Heal;int32 Wood,Food,Day;float Train;ERTSResource Material;int32 Research,Upkeep; };
 inline const FUnit* Table(){static const FUnit Units[]={
 {TEXT("Peasant"),TEXT("工人"),TEXT("TownHall"),70,5,0,0,50,0,3,ERTSResource::Wood,0,0},
 {TEXT("Swordman"),TEXT("剑士"),TEXT("Barracks"),120,15,0,20,50,0,4,ERTSResource::Wood,0,0},
 {TEXT("Archer"),TEXT("弓手"),TEXT("ArcheryRange"),70,12,0,40,40,0,5,ERTSResource::Wood,0,0},
 {TEXT("Spearman"),TEXT("长枪兵"),TEXT("Barracks"),110,14,0,25,45,0,4,ERTSResource::Wood,120,0},
 {TEXT("Scout"),TEXT("斥候"),TEXT("ArcheryRange"),80,9,0,20,40,0,3,ERTSResource::Wood,140,0},
 {TEXT("Priest"),TEXT("牧师"),TEXT("SupportBarracks"),65,0,15,50,60,0,6,ERTSResource::Wood,180,0},
 {TEXT("Paladin"),TEXT("圣骑士"),TEXT("Barracks"),240,27,0,60,100,2,9,ERTSResource::Crystal,140,18},
 {TEXT("Mage"),TEXT("法师"),TEXT("SupportBarracks"),65,34,0,70,60,4,7,ERTSResource::Mana,160,16},
 {TEXT("Light_Infantry"),TEXT("轻步兵"),TEXT("Barracks"),95,12,0,15,40,0,3.5f,ERTSResource::IronOre,90,4},
 {TEXT("HighPriest"),TEXT("大祭司"),TEXT("SupportBarracks"),110,0,28,80,90,4,8,ERTSResource::Mana,220,20},
 {TEXT("King"),TEXT("国王"),TEXT("Barracks"),340,36,0,120,180,6,12,ERTSResource::Gold,360,40},
 {TEXT("Heavy_Infantry"),TEXT("重步兵"),TEXT("Barracks"),215,19,0,45,70,1,6,ERTSResource::RefinedIron,120,10},
 {TEXT("HeavySwordman"),TEXT("重剑士"),TEXT("Barracks"),180,33,0,50,80,1,7,ERTSResource::RefinedIron,160,12},
 {TEXT("Halberdier"),TEXT("戟兵"),TEXT("Barracks"),155,24,0,40,65,1,6,ERTSResource::IronOre,160,10},
 {TEXT("Crossbowman"),TEXT("弩手"),TEXT("ArcheryRange"),90,28,0,60,50,1,6,ERTSResource::RefinedIron,150,12},
 {TEXT("Commander"),TEXT("统帅"),TEXT("Barracks"),230,25,0,90,120,3,9,ERTSResource::Gold,200,24}};return Units;}
 inline int32 Index(FName K){for(int32 I=0;I<Count;++I)if(K==Table()[I].Id)return I;return INDEX_NONE;}
 inline FName Kind(int32 I){return I>=0 && I<Count?FName(Table()[I].Id):NAME_None;}
 inline FString Name(int32 I){return I>=0 && I<Count?Table()[I].Label:TEXT("未知兵种");}
 inline FName Service(FName K){const int32 I=Index(K);return I>=0?FName(Table()[I].Building):NAME_None;}
 inline int32 UnlockDay(FName K){const int32 I=Index(K);return I>=0?Table()[I].Day:999;}
 inline float Growth(int32 L){return FMath::Pow(1.10f,FMath::Clamp(L,1,30)-1);}
 inline float BaseHealth(FName K){const int32 I=Index(K);return I>=0?Table()[I].HP:0;}
 inline float BaseDamage(FName K){const int32 I=Index(K);return I>=0?Table()[I].Damage:0;}
 inline float BaseHeal(FName K){const int32 I=Index(K);return I>=0?Table()[I].Heal:0;}
 inline float TrainSeconds(FName K){const int32 I=Index(K);return I>=0?Table()[I].Train:1;}
 inline TMap<ERTSResource,int32> ResearchCost(FName K){const int32 I=Index(K);if(I<3)return {};const auto& U=Table()[I];if(I<=5)return {{ERTSResource::Wood,U.Research},{ERTSResource::Food,I==3?100:I==4?120:160}};return {{ERTSResource::Wood,120+I*10},{ERTSResource::Food,100+I*10},{U.Material,U.Research}};}
 inline TMap<ERTSResource,int32> TrainCost(FName K,int32 Level=1){const int32 I=Index(K);if(I<0)return {};const auto& U=Table()[I];TMap<ERTSResource,int32> C={{ERTSResource::Wood,U.Wood},{ERTSResource::Food,U.Food}};if(U.Upkeep>0)C.FindOrAdd(U.Material)+=U.Upkeep;const double Multiplier=FMath::Pow(1.10,double(FMath::Clamp(Level,1,30)-1));for(auto& P:C)if(P.Value>0)P.Value=FMath::CeilToInt(P.Value*Multiplier-0.000001);return C;}
 inline TMap<ERTSResource,int32> UpgradeCost(FName K,int32 Target){const int32 I=Index(K);if(I<0 || Target<2 || Target>30)return {};const int32 N=FMath::CeilToInt((45+I*3)*FMath::Pow(1.13f,Target-2)/10)*10;TMap<ERTSResource,int32> C={{ERTSResource::Wood,N},{ERTSResource::Food,N}};if(Target>=6)C.Add(Target<=12?ERTSResource::IronOre:Target<=20?ERTSResource::RefinedIron:Target<=25?ERTSResource::Crystal:ERTSResource::Gold,FMath::CeilToInt(N*.45f));if(Target>=21)C.Add(Target<=25?ERTSResource::Amber:ERTSResource::Mana,FMath::CeilToInt(N*.2f));return C;}
 inline int32 StoreCapacity(ERTSResource R){return R==ERTSResource::Wood || R==ERTSResource::Food?30000:20000;}
}
