#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSAcquisitionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignPopulation.h"
#include "Campaign/RTSCampaignStorage.h"
#include "UI/RTSUnitNameplateStyle.h"
#include "Economy/RTSResourceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNameplateZoomPolicyTest,"MiniRTS.UI.NameplateZoomPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSNameplateZoomPolicyTest::RunTest(const FString&)
{
 using namespace RTSUnitNameplateStyle;
 TestEqual(TEXT("Default zoom keeps full size"),Resolve(2000,false).Scale,1.f);
 TestFalse(TEXT("Near keeps tier description"),Resolve(2000,false).bCompact);
 TestTrue(TEXT("Far simplifies names"),Resolve(3200,false).bCompact);
 TestTrue(TEXT("Far unselected labels suppress overlap"),Resolve(6500,false).bSuppressOverlaps);
 TestFalse(TEXT("Selected names retain priority"),Resolve(6500,true).bSuppressOverlaps);
 TestTrue(TEXT("Far scale bounded"),FMath::IsNearlyEqual(Resolve(9000,false).Scale,.48f,.000001f));
 TestTrue(TEXT("Selected readability floor"),FMath::IsNearlyEqual(Resolve(9000,true).Scale,.72f,.000001f));
 float Previous=1;for(int32 D=2000;D<10000;D+=50){const auto S=Resolve(D,false);TestTrue(TEXT("Zoom scale decreases smoothly"),S.Scale<=Previous+.00001f && S.Scale>=.47999f);Previous=S.Scale;}
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyAllySupportTest,"MiniRTS.Combat.AllyDamageAndSupport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEnemyAllySupportTest::RunTest(const FString&)
{
 const UWorld::InitializationValues Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
 if(!TestNotNull(TEXT("Isolated no-input world"),World))return false;
 ON_SCOPE_EXIT{World->DestroyWorld(false);};
 auto Spawn=[&](FVector Location,uint8 Team,bool Worker=false)
 {
  const FTransform Transform(Location);
  auto* U=World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  auto* Data=NewObject<URTSUnitData>(U);Data->BalanceId=Worker?TEXT("Peasant"):TEXT("Swordman");Data->bWorker=Worker;Data->bAutoAcquireTargets=true;Data->AttackDamage=10;Data->MaxHealth=100;
  U->AutoPossessAI=EAutoPossessAI::Disabled;U->Configure(Data,Team);U->FinishSpawning(Transform);U->SetCombatStance(ERTSCombatStance::Guard);return U;
 };
 auto* Attacker=Spawn(FVector(500,0,82),0);
 auto* Victim=Spawn(FVector(0,0,82),1);
 auto* Helper=Spawn(FVector(-300,0,82),1);
 auto* Worker=Spawn(FVector(-200,200,82),1,true);
 auto* Far=Spawn(FVector(-2100,0,82),1);
 auto* Busy=Spawn(FVector(-200,-200,82),1);
 auto* Other=Spawn(FVector(400,-500,82),0);
 auto* Fragile=Spawn(FVector(-100,400,82),1);Fragile->GetHealth()->RestoreHealthFraction(.2f);
 URTSOrderLibrary::IssueAttackOrder({Busy},Other);
 Victim->GetHealth()->ApplyDamage(1,Attacker);
 auto* Acquisition=Helper->FindComponentByClass<URTSAcquisitionComponent>();
 TestTrue(TEXT("Nearby idle enemy attacks ally's attacker immediately"),Helper->GetCurrentOrder().Type==ERTSOrderType::Attack && Helper->GetCurrentOrder().TargetActor.Get()==Attacker);
 TestTrue(TEXT("Support overrides guard hold without changing stance"),Acquisition->IsSupportingAlly() && !Helper->IsHoldingAutomaticPosition() && Helper->GetCombatStance()==ERTSCombatStance::Guard);
 TestTrue(TEXT("Worker remains uninvolved"),Worker->GetCurrentOrder().Type==ERTSOrderType::None);
 TestTrue(TEXT("Far army remains uninvolved"),Far->GetCurrentOrder().Type==ERTSOrderType::None);
 TestTrue(TEXT("Existing fight is not stolen"),Busy->GetCurrentOrder().TargetActor.Get()==Other);
 TestTrue(TEXT("Low-health retreat candidate remains uninvolved"),Fragile->GetCurrentOrder().Type==ERTSOrderType::None);
 FRTSOrder Queued;Queued.Type=ERTSOrderType::Attack;Queued.TargetActor=Other;Helper->QueueOrder(Queued);
 const int32 QueueCount=Helper->GetQueuedOrderCount();
 Victim->GetHealth()->ApplyDamage(1,Attacker);
 TestEqual(TEXT("Repeated damage never resets helper queue/orders"),Helper->GetQueuedOrderCount(),QueueCount);
 TestTrue(TEXT("Repeated damage keeps the same support target"),Acquisition->IsSupportingAlly() && Helper->GetCurrentOrder().TargetActor.Get()==Attacker);
 Helper->ClearQueuedOrders();
 Attacker->SetActorLocation(FVector(3000,0,82));
 static_cast<UActorComponent*>(Acquisition)->TickComponent(.25f,LEVELTICK_All,nullptr);
 TestFalse(TEXT("Support stops beyond bounded leash"),Acquisition->IsSupportingAlly());
 URTSOrderLibrary::IssueStopOrder({Helper,Victim});Attacker->SetActorLocation(FVector(500,0,82));
 Victim->GetHealth()->ApplyDamage(1000,Attacker);
 TestTrue(TEXT("A killing blow still alerts surviving allies"),Helper->GetCurrentOrder().TargetActor.Get()==Attacker);
 AddInfo(TEXT("ENEMY_ALLY_SUPPORT_OK immediate=1 workers=0 far=0 busyPreserved=1 repeatStable=1 leash=1 lethalAlarm=1"));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPopulationSchemaTest,"MiniRTS.Campaign.PopulationPolicyAndSave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSPopulationSchemaTest::RunTest(const FString&)
{
 using namespace RTSCampaignPopulation;
 TestEqual(TEXT("Initial population unchanged"),Limit(false,0),24);
 TestEqual(TEXT("First housing adds the same eight slots"),Limit(true,0),32);
 TestEqual(TEXT("Repeat purchase adds eight"),Limit(true,1),40);
 TestEqual(TEXT("Maximum player limit"),Limit(true,MaxPurchases),120);
 TestEqual(TEXT("Legacy housing capacity is preserved"),Limit(true,0,32),64);
 TestEqual(TEXT("First expansion wood"),WoodCost(0),160);TestEqual(TEXT("First expansion food"),FoodCost(0),120);
 auto* Stock=NewObject<URTSResourceComponent>();
 Stock->SetAmounts({{ERTSResource::Wood,WoodCost(0)},{ERTSResource::Food,FoodCost(0)-1}});
 const TMap<ERTSResource,int32> FirstCost={{ERTSResource::Wood,WoodCost(0)},{ERTSResource::Food,FoodCost(0)}};
 TestFalse(TEXT("Insufficient food rejects the whole purchase"),Stock->TrySpend(FirstCost));
 TestEqual(TEXT("Rejected purchase retains all wood"),Stock->GetAmount(ERTSResource::Wood),WoodCost(0));
 TestEqual(TEXT("Rejected purchase retains all food"),Stock->GetAmount(ERTSResource::Food),FoodCost(0)-1);
 Stock->Add(ERTSResource::Food,1);TestTrue(TEXT("Exact displayed price is affordable"),Stock->TrySpend(FirstCost));
 TestEqual(TEXT("Successful purchase deducts exact wood"),Stock->GetAmount(ERTSResource::Wood),0);
 TestEqual(TEXT("Successful purchase deducts exact food"),Stock->GetAmount(ERTSResource::Food),0);
 for(int32 I=0;I<MaxPurchases;++I){TestTrue(TEXT("Prices increase each purchase"),WoodCost(I+1)>WoodCost(I) && FoodCost(I+1)>FoodCost(I));}
 TestFalse(TEXT("Negative purchase rejected"),IsValid(-1));TestFalse(TEXT("Overflow purchase rejected"),IsValid(MaxPurchases+1));
 for(int32 I=0;I<=MaxPurchases;++I)
 {
  auto* Save=NewObject<URTSCampaignSave>();Save->Version=URTSCampaignSave::SnapshotVersion;Save->FacilityMask=1;Save->PopulationPurchases=I;
  TArray<uint8> Bytes;if(!TestTrue(TEXT("Encode population snapshot in memory"),RTSCampaignStorage::Encode(Save,Bytes)))return false;
  auto* Copy=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Bytes));
  if(!TestNotNull(TEXT("Bounded decode accepts population schema"),Copy))return false;
  TestEqual(TEXT("Purchased population survives serialization"),Copy->PopulationPurchases,I);
  TestEqual(TEXT("Population limit survives serialization"),Limit(true,Copy->PopulationPurchases),32+I*8);
 }
 auto* Legacy=NewObject<URTSCampaignSave>();Legacy->Version=13;TArray<uint8> Bytes;RTSCampaignStorage::Encode(Legacy,Bytes);
 for(int32 I=0;I<=7;++I)TestEqual(TEXT("Every legacy purchase preserves old limit"),Limit(true,I,32),64+I*8);
 auto* Copy=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Bytes));TestTrue(TEXT("Old schema defaults to no purchased population"),Copy && Copy->PopulationPurchases==0);
 AddInfo(TEXT("POPULATION_MEMORY_SAVE_OK levels=8 legacy13=1 no_disk_writes=1"));return true;
}
#endif
