#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignPalette.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Buildings/RTSBuilding.h"

void ARTSCampaign::ApplyFaction(ARTSUnit* U,int32 Faction)
{
 if(!U || U->GetTeam()!=1 || Faction<0 || Faction>=RTSCampaignFactions::FactionCount) return;
 Factions.Add(U,Faction);
 const FLinearColor Color=RTSCampaignPalette::Color(Faction);
 // The slot is a full unit atlas (skin, metal, weapon and cloth), not a cloth-only slot.
 // Use the pack's recolored atlases rather than replacing every surface with solid color.
 const int32 Cloth=U->GetMesh()->GetMaterialIndex(TEXT("TT_RTS_Units"));
 const FString Variant=RTSCampaignPalette::Index(Faction)==4?TEXT("GreenB"):RTSCampaignPalette::Name(Faction);
 const FString Path=FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Materials/Units/MI_Units_%s.MI_Units_%s"),*Variant,*Variant);
 UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,*Path);
 if(Cloth!=INDEX_NONE && Material) U->GetMesh()->SetMaterial(Cloth,Material);
 TArray<UStaticMeshComponent*> Parts; U->GetComponents(Parts);
 for(auto* Part:Parts) if(Part && (Part->GetName().Contains(TEXT("Selection")) || Part->GetName().Contains(TEXT("Armband"))))
 { if(auto* M=Part->CreateAndSetMaterialInstanceDynamic(0)) M->SetVectorParameterValue(TEXT("Color"),Color); }
}
FRTSCampaignRaidPlan ARTSCampaign::RaidPlan(int32 Day,int32 Faction)
{
 FRTSCampaignRaidPlan P; P.Faction=Faction;
 if(Faction<0 || Faction>=RTSCampaignFactions::FactionCount) return P;
 P.Origin=RTSCampaignFactions::Base[Faction]+FVector(Faction<2?-1200:1200,0,0); // Separate mustering from stationary guards.
 const int32 D=FMath::Clamp(Day,0,2);
 if(Day<=2)
 {
  if(Faction==0)
  {
   P.Swords=D==0?0:D==1?4:6; P.Archers=D==0?0:D==1?4:6; P.Launch=D==0?1100:780;
   P.Target=RTSCampaignMapSpace::FromLegacy(D==1?FVector(-5000,6200,0):FVector(-4300,5100,0));
   P.Intent=D==0?TEXT("观察高地，今日不出征"):D==1?TEXT("远距侦察西坡，不接近主城"):TEXT("在外围伐木线路游走，远程掩护");
  }
  else if(Faction==1)
  {
   P.Swords=D==0?2:D==1?6:8; P.Archers=D==0?1:D==1?2:4; P.Launch=D==0?960:D==1?360:300;
   P.Target=RTSCampaignMapSpace::FromLegacy(D==0?FVector(-3600,-9500,0):D==1?FVector(-3600,-7500,0):FVector(-4200,-6800,0));
   P.Intent=D==0?TEXT("第1分钟远距侦察南部边境，不深入主城"):D==1?TEXT("在南部外围集结，仅侦察粮线"):TEXT("从远处压迫边境，需玩家主动出击");
  }
  else
  {
   P.Swords=D==0?0:Faction==2?3:2; P.Archers=D==0?0:Faction==2?1:2;
   P.Launch=Faction==2?660:960;
   P.Target=RTSCampaignMapSpace::FromLegacy(Faction==2?FVector(-4600,-6200,0):FVector(-4600,6200,0));
   P.Intent=D==0?TEXT("驻守边境，首日不出征"):Faction==2?TEXT("在外围运输线侦察，不接近主城"):TEXT("在外围林地游走，掩护弓手");
  }
 }
 else
 {
  // Late campaign: bigger waves, earlier launches and fronts that creep towards the base every day.
  const int32 G=FMath::Clamp(Day-2,0,10);
  P.Swords=4+G+(Faction==1?2:0); P.Archers=2+G/2+(Faction>=2?1:0); P.Launch=FMath::Clamp(300-G*14,150,300);
  const FVector Home=HomeLocation();
  const FVector BaseTarget=RTSCampaignMapSpace::FromLegacy(Faction==0?FVector(-4300,5100,0):Faction==1?FVector(-4200,-6800,0):Faction==2?FVector(-4600,-6200,0):FVector(-4600,6200,0));
  const double Length=FMath::Max(1.0,FVector::Dist2D(BaseTarget,Home));
  const double Keep=FMath::Max(6000.0*RTSCampaignMapSpace::LayoutScale,Length-FMath::Min(G*650.0,3500.0)*RTSCampaignMapSpace::LayoutScale);
  P.Target=Home+(BaseTarget-Home)*(Keep/Length);
  P.Intent=FString::Printf(TEXT("第%d日加压：每日%d波，逼近至约%d米外集结"),Day+1,EnemyWaveLimit(Day),int32(Keep/100.0));
 }
 P.Launch=FMath::Clamp(150.f-Day*10.f,55.f,150.f);
 const int32 Cap=FMath::Min(18,6+Day*2);P.Swords=(Cap+1)/2;P.Archers=Cap/2;
 P.Target=Day>=2?HomeLocation():HomeLocation()+FVector(2200,(Faction%2?1:-1)*1400,0);
 P.Intent=Day<2?TEXT("付费训练后袭击基地外围，留出开局建设窗口"):TEXT("兵力上限逐日增加，付费成长后进攻大本营；实际规模受库存限制");
 return P;
}
FString ARTSCampaign::GetWarPlan() const
{
 FString S=GuideStep<7?TEXT("引导保护中：敌方经济暂停。\n"):TEXT("敌军以工人采集库存，付费训练与升级（1..30级），不再每日免费升级或凭空增援。\n出击按势力轮换：每天两支势力袭扰，另两支留在自家整备，窗口约105秒后撤回。\n");
 for(int32 F=0;F<EconomyStates.Num();++F){const auto& E=EconomyStates[F];const bool bRaiding=((GetDay()+F)&1)==0;S+=FString::Printf(TEXT("%s：%s · %s\n"),RTSCampaignFactions::Name[F],E.Active?TEXT("经济运作"):TEXT("基地已失守"),E.Deployed?TEXT("出兵袭扰中"):(bRaiding?TEXT("今日待命出击"):TEXT("今日留家整备")));}
 return S;
}
void ARTSCampaign::UpdateWar(float Seconds)
{
 // Retained only as an API compatibility seam. Runtime dispatch belongs to the faction BT.
 // Never spawn timer-funded raids; retreating trained units remain campaign-owned.
}
void ARTSCampaign::CycleFactionColor(int32 Faction)
{
 if(Faction<0 || Faction>=RTSCampaignFactions::FactionCount) return;
 auto& Variable=RTSCampaignPalette::Variable(Faction);
 int32 Next=RTSCampaignPalette::Index(Faction);
 for(int32 Step=0;Step<5;++Step)
 {
  Next=(Next+1)%5; bool Used=false;
  for(int32 Other=0;Other<RTSCampaignFactions::FactionCount;++Other) if(Other!=Faction && RTSCampaignPalette::Index(Other)==Next) Used=true;
  if(!Used) break;
 }
 Variable.AsVariable()->Set(Next,ECVF_SetByCode);
 TArray<TWeakObjectPtr<ARTSUnit>> Units;Factions.GetKeys(Units);
 for(auto Weak:Units)if(auto* Unit=Weak.Get())ApplyFaction(Unit,Factions.FindRef(Weak));
 Message=TEXT("阵营布料配色已切换；阵营身份、战斗关系与数值保持不变。");
}
int32 ARTSCampaign::GetUnitFaction(ARTSUnit* Unit) const { return Factions.FindRef(Unit); }
