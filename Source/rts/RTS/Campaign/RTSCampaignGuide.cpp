#include "Campaign/RTSCampaign.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSGathererComponent.h"
#include "EngineUtils.h"

FString ARTSCampaign::GetGuidance() const
{
 static const TCHAR* Steps[]={
 TEXT("欢迎来到琥珀边境\n敌军控制大部分领土，暂时不要挑战山顶。点击确认开始；引导期间日历与进攻暂停。"),
 TEXT("1 / 6 认识你的工人\n左键选择一名蓝色工人。所有工人开局待命，命令由你发出。"),
 TEXT("2 / 6 下达移动命令\n选中工人，右键附近空地，让他移动至少200厘米。"),
 TEXT("3 / 6 第一载木材\n右键主城东北侧树木，等待采满10木并送回主城。树木需要你亲自下令采集。"),
 TEXT("4 / 6 工人建设与成长\n选中工人，在当前对象面板选择建造研究所，左键空地放置并等待工人完成施工。再选中研究所，升级一次剑士。"),
 TEXT("5 / 6 建立部队\n选中工人建造兵营并等待施工完成，再选中兵营招募一名剑士。"),
 TEXT("6 / 6 阅读战报\n点击左上小地图下的【战报】。黄军擅长袭扰，紫军守卫高地；看当天出兵编制和时间，再确认开启第一天。"),
 TEXT("引导完成\n战役日历已开启。持续采集、永久成长，利用预告准备防守，再争夺矿区。")};
 return Steps[FMath::Clamp(GuideStep,0,7)];
}
void ARTSCampaign::AdvanceGuide()
{
 if(GuideStep==0) GuideStep=1;
 else if(GuideStep==6 && LastGuidePage==0) { GuideStep=7; Message=TEXT("第一天开始：敌方经济已启动；每天两支势力轮换袭扰，实际出兵受库存与战况限制，请查看战报。"); }
}
void ARTSCampaign::NotifyPageOpened(int32 Index) { LastGuidePage=Index; }
void ARTSCampaign::UpdateGuide()
{
 if(GuideStep==1)
 {
  for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData()->bWorker && It->IsSelected())
  { GuideWorker=*It; GuideWorkerStart=It->GetActorLocation(); GuideStep=2; break; }
 }
 if(GuideStep==2 && GuideWorker.IsValid() && FVector::Dist2D(GuideWorker->GetActorLocation(),GuideWorkerStart)>=200) { GuideStep=3; GuideWoodBaseline=URTSBalanceSubsystem::Get(this)->GetResources(0)->GetAmount(ERTSResource::Wood); }
 if(GuideStep==3 && URTSBalanceSubsystem::Get(this)->GetResources(0)->GetAmount(ERTSResource::Wood)>=GuideWoodBaseline+10) GuideStep=4;
 if(GuideStep==4 && SwordLevel>=2) GuideStep=5;
 if(GuideStep==5)
 {
  int32 Swords=0; for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData()->BalanceId==TEXT("Swordman")) ++Swords;
  if(Swords>=5) GuideStep=6;
 }
}
