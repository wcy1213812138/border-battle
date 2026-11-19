#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Campaign/RTSCampaignEnemyProgression.h"
#include "Campaign/RTSCampaignEnemyResourcePlacement.h"

namespace
{
 TMap<ERTSResource,int32> EnemyBillWithReserve(const TMap<ERTSResource,int32>& Cost)
 {
  TMap<ERTSResource,int32> Stock={{ERTSResource::Wood,150},{ERTSResource::Food,150}};
  for(const auto& P:Cost)Stock.FindOrAdd(P.Key)+=P.Value;
  return Stock;
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyPacingCeilingTest,"MiniRTS.EnemyEconomy.PaidPurchaseCeiling",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEnemyPacingCeilingTest::RunTest(const FString&)
{
 using namespace RTSCampaignEnemyProgression;
 for(int32 Day=0;Day<=40;++Day)
 {
  TestEqual(TEXT("Zero-based day purchase ceiling"),MaxNextLevel(Day),FMath::Min(30,5+2*Day));
  TMap<FName,int32> Levels;Levels.Add(TEXT("Swordman"),MaxNextLevel(Day));
  auto* Account=NewObject<URTSResourceComponent>();Account->SetAmounts({{ERTSResource::Wood,30000},{ERTSResource::Food,30000}});
  TestFalse(TEXT("Rich account cannot bypass day ceiling"),BuyUpgrade(Levels,Account,TEXT("Swordman"),Day));
  TestEqual(TEXT("Ceiling rejection does not charge"),Account->GetAmount(ERTSResource::Wood),30000);
  for(FName Basic:{FName("Peasant"),FName("Swordman"),FName("Archer")})TestTrue(TEXT("Basic units stay unlocked every day"),IsUnlocked({},Basic,Day));
  TestTrue(TEXT("Barracks always retains basic fallback"),Recruitment({},TEXT("Barracks"),Day,{}).Contains(FName("Swordman")));
  TestTrue(TEXT("Range always retains basic fallback"),Recruitment({},TEXT("ArcheryRange"),Day,{}).Contains(FName("Archer")));
 }
 TestEqual(TEXT("Worker demand starts at four, not four free workers"),DesiredWorkers(0),4);
 TestEqual(TEXT("Second day paid worker target"),DesiredWorkers(1),5);
 TestEqual(TEXT("Paid worker target is bounded"),DesiredWorkers(100),6);
 TMap<FName,int32> Levels;auto* Account=NewObject<URTSResourceComponent>();
 // Four consecutive purchases reach L5 on day zero, each with exact cost and reserve.
 for(int32 Target=2;Target<=5;++Target)
 {
  const auto Cost=RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),Target);const auto Before=EnemyBillWithReserve(Cost);Account->SetAmounts(Before);
  if(!TestTrue(TEXT("Faster level still requires a real purchase"),BuyUpgrade(Levels,Account,TEXT("Swordman"),0)))return false;
  TestEqual(TEXT("Only one paid level per call"),Levels.FindRef(TEXT("Swordman")),Target);
  for(int32 R=0;R<static_cast<int32>(ERTSResource::Count);++R){const auto Type=static_cast<ERTSResource>(R);TestEqual(TEXT("Exact upgrade deduction"),Account->GetAmount(Type),Before.FindRef(Type)-Cost.FindRef(Type));}
 }
 const auto Cost=RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),6);
 for(const auto& Short:Cost)
 {
  auto Before=EnemyBillWithReserve(Cost);--Before.FindOrAdd(Short.Key);Account->SetAmounts(Before);
  TestFalse(TEXT("One short of cost plus reserve rejects"),BuyUpgrade(Levels,Account,TEXT("Swordman"),1));
  TestEqual(TEXT("Insufficient upgrade keeps level"),Levels.FindRef(TEXT("Swordman")),5);
  for(const auto& P:Before)TestEqual(TEXT("Insufficient upgrade is atomic"),Account->GetAmount(P.Key),P.Value);
 }
 const auto Before=EnemyBillWithReserve(Cost);Account->SetAmounts(Before);
 TestFalse(TEXT("Next day cost cannot bypass day zero ceiling"),BuyUpgrade(Levels,Account,TEXT("Swordman"),0));
 TestTrue(TEXT("Day one permits paid L6 with iron"),BuyUpgrade(Levels,Account,TEXT("Swordman"),1));
 for(const auto& P:Before)TestEqual(TEXT("Higher upgrade exact deduction including ore"),Account->GetAmount(P.Key),P.Value-Cost.FindRef(P.Key));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyResearchPacingTest,"MiniRTS.EnemyEconomy.ResearchDaySchedule",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEnemyResearchPacingTest::RunTest(const FString&)
{
 using namespace RTSCampaignEnemyProgression;
 const FName Plan[]={TEXT("Spearman"),TEXT("Scout"),TEXT("Light_Infantry"),TEXT("Heavy_Infantry"),TEXT("Crossbowman"),TEXT("HeavySwordman"),TEXT("Halberdier"),TEXT("Paladin"),TEXT("Commander"),TEXT("King")};
 for(int32 I=0;I<UE_ARRAY_COUNT(Plan);++I)
 {
  const FName K=Plan[I],Service=RTSCampaignProgression::Service(K);const int32 Day=EffectiveResearchDay(K);
  TestEqual(TEXT("Research moved one day earlier, never before day zero"),Day,FMath::Max(0,RTSCampaignProgression::UnlockDay(K)-1));
  TMap<FName,int32> Levels;for(int32 J=0;J<I;++J)Levels.Add(Plan[J],1);
  const auto Cost=RTSCampaignProgression::ResearchCost(K);const auto Before=EnemyBillWithReserve(Cost);auto* Account=NewObject<URTSResourceComponent>();Account->SetAmounts(Before);
  TestFalse(TEXT("Day alone never grants advanced research"),IsUnlocked(Levels,K,Day));
  TestFalse(TEXT("Missing research never appears in recruitment"),Recruitment(Levels,Service,Day,{}).Contains(K));
  TestTrue(TEXT("All gates agree before effective day"),NextResearch(Levels,Service,Day-1)!=K);
  auto PaidTooEarly=Levels;PaidTooEarly.Add(K,1);
  TestFalse(TEXT("Even a stored paid entry obeys the same effective day"),IsUnlocked(PaidTooEarly,K,Day-1));
  TestFalse(TEXT("Cannot buy before effective day"),BuyResearch(Levels,Account,K,Service,Day-1));
  for(const auto& P:Before)TestEqual(TEXT("Early research is atomic"),Account->GetAmount(P.Key),P.Value);
  TestTrue(TEXT("NextResearch uses effective day"),NextResearch(Levels,Service,Day)==K);
  TestFalse(TEXT("No free support service"),BuyResearch(Levels,Account,K,TEXT("SupportBarracks"),Day));
  for(const auto& Short:Cost)
  {
   auto Poor=Before;--Poor.FindOrAdd(Short.Key);Account->SetAmounts(Poor);
   TestFalse(TEXT("Insufficient research rejects"),BuyResearch(Levels,Account,K,Service,Day));
   TestFalse(TEXT("Rejected research never unlocks"),IsUnlocked(Levels,K,Day));
   for(const auto& P:Poor)TestEqual(TEXT("Insufficient research is atomic"),Account->GetAmount(P.Key),P.Value);
  }
  Account->SetAmounts(Before);TestTrue(TEXT("Research bought on effective day"),BuyResearch(Levels,Account,K,Service,Day));
  TestEqual(TEXT("Research is paid level one, not a free higher level"),Levels.FindRef(K),1);
  for(const auto& P:Before)TestEqual(TEXT("Exact research deduction"),Account->GetAmount(P.Key),P.Value-Cost.FindRef(P.Key));
  TestTrue(TEXT("IsUnlocked agrees with successful purchase"),IsUnlocked(Levels,K,Day));
  TestTrue(TEXT("Recruitment agrees with successful purchase"),Recruitment(Levels,Service,Day,{}).Contains(K));
  TestFalse(TEXT("Duplicate research cannot charge twice"),BuyResearch(Levels,Account,K,Service,Day));
  for(const auto& P:Before)TestEqual(TEXT("Duplicate research leaves every resource unchanged"),Account->GetAmount(P.Key),P.Value-Cost.FindRef(P.Key));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyResourceSeedTest,"MiniRTS.EnemyEconomy.ResourceCandidateSeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEnemyResourceSeedTest::RunTest(const FString&)
{
 using namespace RTSCampaignEnemyResourcePlacement;
 const FString CampaignId(TEXT("enemy-layout-repeatable-001"));
 TestTrue(TEXT("Faction affects seed"),Seed(CampaignId,0)!=Seed(CampaignId,1));
 TestTrue(TEXT("Campaign affects seed"),Seed(CampaignId,0)!=Seed(TEXT("enemy-layout-repeatable-002"),0));
 for(int32 F=0;F<4;++F)
 {
  FRandomStream A(static_cast<int32>(Seed(CampaignId,F))),B(static_cast<int32>(Seed(CampaignId,F)));
  double MinDistance=TNumericLimits<double>::Max(),MaxDistance=0;
  for(int32 I=0;I<32;++I)
  {
   const FVector P=Candidate(A,RTSCampaignFactions::Base[F],FVector::ZeroVector),Q=Candidate(B,RTSCampaignFactions::Base[F],FVector::ZeroVector);
   TestTrue(TEXT("Same campaign/faction gives same candidate sequence"),P.Equals(Q,0));
   const double D=FVector::Dist2D(P,RTSCampaignFactions::Base[F]+FVector(0,1100,0));MinDistance=FMath::Min(MinDistance,D);MaxDistance=FMath::Max(MaxDistance,D);
  }
  TestTrue(TEXT("Candidates are not an equal-distance Hall ring"),MaxDistance-MinDistance>500);
 }
 AddInfo(TEXT("Runtime follow-up: authored campaign map; repeat same campaign/faction seed; eight finite nodes per active faction; four real work slots and Hall haul/deposit; nav update + save/load unchanged feet; legacy failure rollback with obstructed terrain. No runtime map/editor is started by this test."));
 return true;
}
#endif
