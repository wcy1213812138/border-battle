#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignEnemyProgression.h"
#include "Campaign/RTSCampaignStorage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyPaidPolicyTest,"MiniRTS.EnemyEconomy.PaidProgressionPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEnemyPaidPolicyTest::RunTest(const FString&)
{
 using namespace RTSCampaignEnemyProgression;
 TMap<FName,int32> Levels;auto* Account=NewObject<URTSResourceComponent>();
 Account->SetAmounts({{ERTSResource::Wood,250},{ERTSResource::Food,250}});
 const auto Upgrade=RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),2);
 TestTrue(TEXT("250 staples can buy real early upgrade below old 600 gate"),BuyUpgrade(Levels,Account,TEXT("Swordman"),0));
 TestEqual(TEXT("Exact upgrade wood"),Account->GetAmount(ERTSResource::Wood),250-Upgrade.FindRef(ERTSResource::Wood));
 TestEqual(TEXT("Exact upgrade food"),Account->GetAmount(ERTSResource::Food),250-Upgrade.FindRef(ERTSResource::Food));
 TestEqual(TEXT("Paid level recorded"),Levels.FindRef(TEXT("Swordman")),2);
 TestFalse(TEXT("Advanced levels cannot bypass research"),BuyUpgrade(Levels,Account,TEXT("Spearman"),0));
 Account->SetAmounts({{ERTSResource::Wood,1000},{ERTSResource::Food,1000}});
 TestFalse(TEXT("Wrong producer rejects research"),BuyResearch(Levels,Account,TEXT("Spearman"),TEXT("ArcheryRange"),0));
 TestFalse(TEXT("Day gate rejects early research"),BuyResearch(Levels,Account,TEXT("HeavySwordman"),TEXT("Barracks"),0));
 TestFalse(TEXT("Missing refined iron rejects atomically"),BuyResearch(Levels,Account,TEXT("HeavySwordman"),TEXT("Barracks"),2));
 TestEqual(TEXT("Rejected research keeps wood"),Account->GetAmount(ERTSResource::Wood),1000);
 TestFalse(TEXT("Rejected research stays locked"),IsUnlocked(Levels,TEXT("HeavySwordman"),2));
 TestTrue(TEXT("Real research purchase"),BuyResearch(Levels,Account,TEXT("Spearman"),TEXT("Barracks"),0));
 const auto Research=RTSCampaignProgression::ResearchCost(TEXT("Spearman"));
 TestEqual(TEXT("Exact research wood"),Account->GetAmount(ERTSResource::Wood),1000-Research.FindRef(ERTSResource::Wood));
 TestEqual(TEXT("Exact research food"),Account->GetAmount(ERTSResource::Food),1000-Research.FindRef(ERTSResource::Food));
 const int32 Wood=Account->GetAmount(ERTSResource::Wood);
 TestFalse(TEXT("No duplicate research purchase"),BuyResearch(Levels,Account,TEXT("Spearman"),TEXT("Barracks"),0));
 TestEqual(TEXT("Duplicate doesn't charge"),Account->GetAmount(ERTSResource::Wood),Wood);
 const auto Candidates=Recruitment(Levels,TEXT("Barracks"),0,{});
 TestTrue(TEXT("Paid advanced candidate before basic"),Candidates.Num()==2 && Candidates[0]==TEXT("Spearman") && Candidates[1]==TEXT("Swordman"));
 TestFalse(TEXT("No free advanced candidates"),Candidates.Contains(FName("HeavySwordman")));
 Account->SetAmounts({{ERTSResource::Wood,199},{ERTSResource::Food,1000}});Levels.Remove(TEXT("Archer"));
 TestFalse(TEXT("Keep 150 staple reserve"),BuyUpgrade(Levels,Account,TEXT("Archer"),0));
 TestEqual(TEXT("Reserve rejection is atomic"),Account->GetAmount(ERTSResource::Food),1000);
 Levels.Add(TEXT("Swordman"),MaxNextLevel(0));Account->SetAmounts({{ERTSResource::Wood,1000},{ERTSResource::Food,1000}});
 TestFalse(TEXT("Paid day-zero level ceiling is bounded at five"),BuyUpgrade(Levels,Account,TEXT("Swordman"),0));
 for(int32 Version:{13,URTSCampaignSave::SnapshotVersion})
 {
  auto* Save=NewObject<URTSCampaignSave>();Save->Version=Version;Save->FactionEconomies.SetNum(4);Save->FactionEconomies[0].Levels=Levels;
  TArray<uint8> Bytes;if(!TestTrue(TEXT("Encode existing faction schema"),RTSCampaignStorage::Encode(Save,Bytes)))return false;
  auto* Copy=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Bytes));
  if(!TestNotNull(TEXT("Decode faction schema"),Copy))return false;
  TestTrue(TEXT("Paid research survives memory roundtrip"),Copy->FactionEconomies.Num()==4 && Copy->FactionEconomies[0].Levels.OrderIndependentCompareEqual(Levels));
  TestTrue(TEXT("Unresearched kinds remain absent"),!Copy->FactionEconomies[0].Levels.Contains(TEXT("HeavySwordman")));
 }
 AddInfo(TEXT("ENEMY_PAID_POLICY_OK exactCosts=1 insufficientAtomic=1 reserve=150 researchGate=1 dayGate=1 legacyMemoryRoundtrip=1 diskWrites=0"));return true;
}
#endif

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSProducerComponent.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationSystem.h"

struct FRTSCampaignEnemyTestAccess
{
 static void Setup(ARTSCampaign* C,URTSResourceComponent* Account,const TArray<ARTSBuilding*>& Buildings,const TArray<ARTSUnit*>& Units)
 {
  C->SetActorTickEnabled(false);C->bReady=true;C->GuideStep=7;C->Elapsed=0;C->EconomyStates.SetNum(4);C->FactionAccounts.SetNum(4);C->FactionAccounts[0]=Account;
  C->EconomyStates[0].NextRaid=10000;for(auto* B:Buildings)C->Buildings.Add(B);
  for(auto* B:Buildings)C->EconomicFactions.Add(B,0);
  for(auto* U:Units)C->EconomicFactions.Add(U,0);
 }
 static TMap<FName,int32>& Levels(ARTSCampaign* C){return C->EconomyStates[0].Levels;}
 static void AddNode(ARTSCampaign* C,ARTSResourceNode* N){C->EnemyNodes.Add(N);C->EnemyNodeFactions.Add(0);}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyPaidProductionTest,"MiniRTS.EnemyEconomy.RealPaidProduction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
namespace
{
 class FCheckEnemyPaidProduction : public IAutomationLatentCommand
 {
 public:
  explicit FCheckEnemyPaidProduction(FAutomationTestBase* T):Test(T){}
  bool Update() override
  {
   if(FPlatformTime::Seconds()-Started>45){Test->AddError(TEXT("Paid enemy production watchdog"));return true;}
   UWorld* W=GEditor?GEditor->PlayWorld:nullptr;if(!W || !W->HasBegunPlay())return false;
   if(Stage==0)
   {
    auto* Balance=URTSBalanceSubsystem::Get(W);if(!Balance)return false;
    if(auto* M=URTSMatchSubsystem::Get(W))M->EnterSandbox();
    Campaign=W->SpawnActor<ARTSCampaign>();Campaign->SetActorTickEnabled(false);
    auto Building=[&](FName K,FVector P){FTransform T(P);auto* B=W->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);B->Configure(K,1,true);B->FinishSpawning(T);return B;};
    Hall=Building(TEXT("TownHall"),FVector(-1600,-1400,0));Barracks=Building(TEXT("Barracks"),FVector(-600,-1400,0));Range=Building(TEXT("ArcheryRange"),FVector(400,-1400,0));
    TArray<ARTSUnit*> Units;
    for(int32 I=0;I<7;++I)
    {
     const FName K=I<4?FName("Peasant"):I<6?FName("Swordman"):FName("Archer");
     FTransform T(FVector(-1500+I*180,-700,82));auto* U=W->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
     U->Configure(Balance->CreateUnitData(K,W),1);U->FinishSpawning(T);U->SetCombatStance(ERTSCombatStance::Guard);Units.Add(U);
    }
    Account=NewObject<URTSResourceComponent>(Campaign);Account->SetAmounts({{ERTSResource::Wood,600},{ERTSResource::Food,590}});
    FTransform NT(FVector(-1800,500,90));auto* Ore=W->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),NT,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Ore->Configure(ERTSResource::IronOre,500);Ore->SetOwningTeam(1);Ore->FinishSpawning(NT);FRTSCampaignEnemyTestAccess::AddNode(Campaign,Ore);
    FRTSCampaignEnemyTestAccess::Setup(Campaign,Account,{Hall,Barracks,Range},Units);
    At=W->GetTimeSeconds();Stage=1;return false;
   }
   if(Stage==1)
   {
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(W->GetTimeSeconds()-At<2 || !Nav || Nav->IsNavigationBuildInProgress())return false;
    FString Error;
    Test->TestFalse(TEXT("Production rejects unresearched advanced kind"),Barracks->GetProducer()->Enqueue(TEXT("Spearman"),Error));
    Test->TestEqual(TEXT("Rejected order does not charge"),Account->GetAmount(ERTSResource::Wood),600);
    Campaign->StepFactionEconomy(0);
    auto& Levels=FRTSCampaignEnemyTestAccess::Levels(Campaign);
    Test->TestTrue(TEXT("Step bought both research lines"),Levels.Contains(TEXT("Spearman")) && Levels.Contains(TEXT("Scout")));
    Test->TestEqual(TEXT("Step bought real early Swordman upgrade"),Levels.FindRef(TEXT("Swordman")),2);
    const auto& BQ=Barracks->GetProducer()->GetQueue();const auto& RQ=Range->GetProducer()->GetQueue();
    Test->TestTrue(TEXT("Real producer contains advanced paid orders"),BQ.Num()==1 && BQ[0]==TEXT("Spearman") && RQ.Num()==1 && RQ[0]==TEXT("Scout"));
    TMap<ERTSResource,int32> Bill;
    for(auto Cost:{RTSCampaignProgression::ResearchCost(TEXT("Spearman")),RTSCampaignProgression::ResearchCost(TEXT("Scout")),RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),2),RTSCampaignProgression::TrainCost(TEXT("Spearman")),RTSCampaignProgression::TrainCost(TEXT("Scout"))})for(const auto& P:Cost)Bill.FindOrAdd(P.Key)+=P.Value;
    Test->TestEqual(TEXT("Combined real wood charges"),Account->GetAmount(ERTSResource::Wood),600-Bill.FindRef(ERTSResource::Wood));
    Test->TestEqual(TEXT("Combined real food charges"),Account->GetAmount(ERTSResource::Food),590-Bill.FindRef(ERTSResource::Food));
    Test->TestTrue(TEXT("Pay a level before advanced spawn"),RTSCampaignEnemyProgression::BuyUpgrade(Levels,Account,TEXT("Spearman"),0));
    At=W->GetTimeSeconds();Stage=2;return false;
   }
   bool Spear=false,Scout=false;
   for(TActorIterator<ARTSUnit> It(W);It;++It)if(Campaign->GetActorFaction(*It)==0 && It->IsAlive())
   {
    const auto* D=It->GetUnitData();
    if(D->BalanceId==TEXT("Spearman")){Spear=true;Test->TestTrue(TEXT("Produced advanced soldier inherits paid level"),FMath::IsNearlyEqual(D->MaxHealth,RTSCampaignProgression::BaseHealth(TEXT("Spearman"))*RTSCampaignProgression::Growth(2),.01f));}
    if(D->BalanceId==TEXT("Scout"))Scout=true;
   }
   if(Spear && Scout)
   {
    // Additional idle workers must be able to acquire the next real research bill.
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(Campaign->GetActorFaction(*It)==0 && It->GetUnitData()->bWorker){FRTSOrder Stop;Stop.Type=ERTSOrderType::Stop;It->ReceiveOrder(Stop);}
    Account->SetAmounts({{ERTSResource::Wood,400},{ERTSResource::Food,400}});Campaign->StepFactionEconomy(0);
    bool GatheringOre=false;for(TActorIterator<ARTSUnit> It(W);It;++It)if(Campaign->GetActorFaction(*It)==0 && It->GetUnitData()->bWorker && It->GetCurrentOrder().Type==ERTSOrderType::Gather)
    {const auto* N=Cast<ARTSResourceNode>(It->GetCurrentOrder().TargetActor.Get());if(N && N->GetResourceType()==ERTSResource::IronOre)GatheringOre=true;}
    Test->TestTrue(TEXT("Worker assigned real missing research material"),GatheringOre);
    Test->AddInfo(TEXT("ENEMY_REAL_PRODUCTION_OK researchPaid=1 upgradePaid=1 advancedOrdersPaid=1 producedSpearman=1 producedScout=1 inheritedLevel=2 materialGatherAssigned=1 injectedPlayerStock=0"));return true;
   }
   if(W->GetTimeSeconds()-At>20){Test->AddError(TEXT("Real paid advanced production did not complete within 20s"));return true;}return false;
  }
 private:
  FAutomationTestBase* Test;ARTSCampaign* Campaign=nullptr;URTSResourceComponent* Account=nullptr;ARTSBuilding* Hall=nullptr;ARTSBuilding* Barracks=nullptr;ARTSBuilding* Range=nullptr;
  int32 Stage=0;double Started=FPlatformTime::Seconds(),At=0;
 };
}
bool FRTSEnemyPaidProductionTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckEnemyPaidProduction(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
