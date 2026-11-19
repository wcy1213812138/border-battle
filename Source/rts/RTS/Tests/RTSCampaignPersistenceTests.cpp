#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignStorage.h"
#include "Selection/RTSSelectionComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Campaign/RTSCampaignEntry.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Core/RTSHumanController.h"
#include "Units/RTSUnit.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSResourceComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/BoxComponent.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"
#include "Tests/RTSAutomationSaveScope.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Components/PrimitiveComponent.h"
#include "AIController.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Campaign/RTSCampaignWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Campaign/RTSCampaignEconomyController.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

struct FRTSCampaignPersistenceTestAccess
{
 static void Prepare(ARTSCampaign* C){C->GuideStep=7;C->Elapsed=0;C->SpawnDay=C->RaidDay=0;C->SpawnMask=3;C->RaidMask=15;C->RaidWaves.Init(0,4);}
 static URTSCampaignSave* Capture(ARTSCampaign* C,FString& Error){return C->CaptureSnapshot(Error);}
 static bool Validate(ARTSCampaign* C,URTSCampaignSave* S){return C->ValidateSave(S);}
 static ARTSBuilding* ProducerFixture(ARTSCampaign* C,FName Kind)
 {
  if(auto* Existing=C->FindServiceBuilding(Kind))return Existing;
  FString Error;auto* Balance=URTSBalanceSubsystem::Get(C);
  for(int32 I=0;I<8;++I)
  {
   const double A=I*UE_PI/4;
   if(auto* B=Balance->PlaceBuilding(Kind,0,RTSCampaignTerrain::Ground(C->HomeLocation()+FVector(FMath::Cos(A)*1400,FMath::Sin(A)*1400,0)),Error))
   {B->AddConstructionWork(1000);return B;} // Explicit paid persistence fixture, not a natural-build claim.
  }
  return nullptr;
 }
 static ARTSUnit* CrowdFixture(ARTSCampaign* C,const FVector& Feet){return C->Spawn(TEXT("Peasant"),0,Feet);}
 static void PopulationFixture(ARTSCampaign* C,int Purchases=0,int Bonus=0){C->SetActorTickEnabled(false);C->FacilityMask|=1;C->PopulationPurchases=Purchases;C->LegacyPopulationBonus=Bonus;}
 static ARTSResourceNode* EnemyNode(ARTSCampaign* C,int32 F,ERTSResource Type){for(int32 I=0;I<C->EnemyNodes.Num();++I)if(C->EnemyNodeFactions[I]==F && C->EnemyNodes[I]->GetResourceType()==Type)return C->EnemyNodes[I];return nullptr;}
 static bool Restore(ARTSCampaign* C,URTSCampaignSave* S,FString& Error){return C->ValidateSave(S) && C->PreflightSnapshot(S,Error) && C->RestoreSnapshot(S,Error);}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEconomySnapshotValidation,"MiniRTS.Persistence.SnapshotValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEconomySnapshotValidation::RunTest(const FString&)
{
 FRTSGathererSnapshot G;G.Carried=7;G.WorkElapsed=.2f;G.bReturning=true;
 TestTrue(TEXT("Partial cargo and interval valid"),URTSGathererComponent::IsValidEconomy(G,10));G.Carried=11;TestFalse(TEXT("Overflow cargo rejected"),URTSGathererComponent::IsValidEconomy(G,10));G.Carried=-1;TestFalse(TEXT("Negative cargo rejected"),URTSGathererComponent::IsValidEconomy(G,10));G.Carried=0;G.WorkElapsed=-.1f;TestFalse(TEXT("Negative fractional work rejected"),URTSGathererComponent::IsValidEconomy(G,10));
 FRTSProductionSnapshot P;FRTSPaidTrainingSnapshot Paid;Paid.Kind=TEXT("Swordman");Paid.Cost={{ERTSResource::Wood,40},{ERTSResource::Food,60}};Paid.TrainSeconds=4;P.Orders={Paid,Paid};P.TrainingSeconds=1.7f;
 TestTrue(TEXT("Immutable paid FIFO valid"),URTSProducerComponent::IsValidProduction(P,TEXT("Barracks"),true,true));
 TestFalse(TEXT("Unfinished producer cannot have orders"),URTSProducerComponent::IsValidProduction(P,TEXT("Barracks"),false,true));
 TestFalse(TEXT("Wrong training service rejected"),URTSProducerComponent::IsValidProduction(P,TEXT("TownHall"),true,true));
 P.TrainingSeconds=5;TestFalse(TEXT("Over-complete head rejected"),URTSProducerComponent::IsValidProduction(P,TEXT("Barracks"),true,true));P.TrainingSeconds=4;P.bSpawnBlocked=true;TestTrue(TEXT("Blocked at 100 percent valid"),URTSProducerComponent::IsValidProduction(P,TEXT("Barracks"),true,true));P.Orders[0].Cost.Add(ERTSResource::Wood,-1);TestFalse(TEXT("Negative refund exploit rejected"),URTSProducerComponent::IsValidProduction(P,TEXT("Barracks"),true,true));
 auto* S=NewObject<URTSCampaignSave>();TestEqual(TEXT("Historical omitted Version default remains 10"),S->Version,10);S->Version=12;TestTrue(TEXT("Schema12 already compact, no second shrink"),ARTSCampaign::MigrateCompactLayout(S));S->Version=13;TestTrue(TEXT("Schema13 already compact"),ARTSCampaign::MigrateCompactLayout(S));S->Version=URTSCampaignSave::SnapshotVersion+1;TestFalse(TEXT("Future schema rejected"),ARTSCampaign::MigrateCompactLayout(S));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignBoundedReader,"MiniRTS.Persistence.BoundedCorruptFileReader",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignBoundedReader::RunTest(const FString&)
{
 auto* S=NewObject<URTSCampaignSave>();S->Version=URTSCampaignSave::SnapshotVersion;S->CampaignId=TEXT("BoundedReader");S->RegionSpawnValid={true,false,true};S->Stock={{ERTSResource::Wood,123},{ERTSResource::Food,456}};
 FRTSFactionEconomySave Enemy;Enemy.Stock={{ERTSResource::Wood,42},{ERTSResource::Food,77}};Enemy.Levels={{FName("Swordman"),3}};Enemy.NextRaid=123;Enemy.Deployed=true;Enemy.RetreatAt=300;S->FactionEconomies.Add(Enemy);S->BuildingTeams.Add(1);S->BuildingFactions.Add(0);FRTSEnemyNodeSave Local;Local.Remaining=137;Local.Capture.Seconds=1.25;Local.Capture.Team=0;S->EnemyNodes.Add(Local);
 FRTSCampaignUnitSave Unit;Unit.Kind=TEXT("Peasant");Unit.Feet=FVector(1,2,3);Unit.OrderType=ERTSOrderType::Gather;Unit.Economy.Carried=2;Unit.Economy.bReturning=true;Unit.Economy.WorkElapsed=.25f;S->Units.Add(Unit);
 FRTSCampaignBuildingWorkSave Work;FRTSPaidTrainingSnapshot Paid;Paid.Kind=TEXT("Swordman");Paid.TrainSeconds=4;Paid.Cost={{ERTSResource::Wood,40},{ERTSResource::Food,60}};Work.Production.Orders.Add(Paid);Work.Production.TrainingSeconds=.7f;S->BuildingWork.Add(Work);
 TArray<uint8> Bytes;TestTrue(TEXT("Integrity encoding"),RTSCampaignStorage::Encode(S,Bytes));auto* Decoded=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Bytes));
 if(!TestNotNull(TEXT("Bounded reader accepts exact new schema"),Decoded))return false;
 bool Equal=true;for(TFieldIterator<FProperty> It(S->GetClass());It;++It)Equal &= It->Identical_InContainer(S,Decoded);TestTrue(TEXT("Nested snapshot exactly round trips"),Equal);
 TestNull(TEXT("Four-byte crash fixture rejected"),RTSCampaignStorage::Decode({0,1,2,3}));
 for(int32 Cut=0;Cut<Bytes.Num();Cut+=FMath::Max(1,Bytes.Num()/80)){TArray<uint8> Truncated;Truncated.Append(Bytes.GetData(),Cut);if(RTSCampaignStorage::Decode(Truncated)){AddError(FString::Printf(TEXT("Truncation accepted at %d"),Cut));return false;}}
 for(int32 I=0;I<Bytes.Num();I+=FMath::Max(1,Bytes.Num()/80)){auto Changed=Bytes;Changed[I]^=0x80;if(RTSCampaignStorage::Decode(Changed)){AddError(FString::Printf(TEXT("Corruption accepted at %d"),I));return false;}}
 S->Version=11;TArray<uint8> Legacy;UGameplayStatics::SaveGameToMemory(S,Legacy);TestNotNull(TEXT("Legacy layout11 raw GVAS remains readable"),RTSCampaignStorage::Decode(Legacy));
 S->Version=10;Legacy.Reset();UGameplayStatics::SaveGameToMemory(S,Legacy);auto* Old=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Legacy));TestTrue(TEXT("Omitted historical default stays10"),Old && Old->Version==10);
 // These raw legacy mutations have no checksum; bounded structure parsing must still never assert.
 FRandomStream Random(120011);for(int32 I=0;I<256;++I){auto Changed=Legacy;const int32 At=Random.RandRange(0,Changed.Num()-1);Changed[At]^=uint8(1<<Random.RandRange(0,7));RTSCampaignStorage::Decode(Changed);}
 TArray<uint8> Fixture;const FString FixturePath=FPaths::ProjectSavedDir()/TEXT("Automation/Compact-Final/LegacyV10Fixture.sav");
 if(TestTrue(TEXT("Actual historical fixture exists"),FFileHelper::LoadFileToArray(Fixture,*FixturePath))){auto* Actual=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Fixture));TestTrue(TEXT("Actual omittedVersion fixture has117units/5buildings"),Actual && Actual->Version==10 && Actual->Units.Num()==117 && Actual->BuildingKinds.Num()==5);}
 AddInfo(TEXT("BOUNDED_SAVE_READER_OK truncation=80 corruption=80 legacy_mutations=256 actual_v10=1"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignStorageRecovery,"MiniRTS.Persistence.AtomicWriteAndBackup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignStorageRecovery::RunTest(const FString&)
{
 const FString Slot=TEXT("Automation_Storage_")+FGuid::NewGuid().ToString(EGuidFormats::Digits),Folder=FPaths::ProjectSavedDir()/TEXT("SaveGames"),Primary=Folder/(Slot+TEXT(".sav")),Previous=Folder/(RTSCampaignStorage::PreviousSlot(Slot)+TEXT(".sav"));
 ON_SCOPE_EXIT{UGameplayStatics::DeleteGameInSlot(Slot,0);UGameplayStatics::DeleteGameInSlot(RTSCampaignStorage::PreviousSlot(Slot),0);};
 UGameplayStatics::DeleteGameInSlot(Slot,0);UGameplayStatics::DeleteGameInSlot(RTSCampaignStorage::PreviousSlot(Slot),0);
 auto* S=NewObject<URTSCampaignSave>();S->Version=12;S->CampaignId=TEXT("AtomicWriter");S->Elapsed=1;FString Error;
 auto Valid=[](const USaveGame* Candidate){return Cast<URTSCampaignSave>(Candidate)!=nullptr;};
 if(!TestTrue(TEXT("First verified write"),RTSCampaignStorage::Write(S,Slot,Valid,Error)))return false;
 S->Elapsed=2;if(!TestTrue(TEXT("Second write rotates old valid record"),RTSCampaignStorage::Write(S,Slot,Valid,Error)))return false;
 auto* Backup=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(RTSCampaignStorage::PreviousSlot(Slot)));TestTrue(TEXT("Backup is previous, not new"),Backup && Backup->Elapsed==1);
 TArray<uint8> Before,BackupBytes,After;FFileHelper::LoadFileToArray(Before,*Primary);FFileHelper::LoadFileToArray(BackupBytes,*Previous);
#if PLATFORM_WINDOWS
 // A real sharing violation prevents replacement, without test-only production hooks.
 HANDLE Lock=::CreateFileW(*FPaths::ConvertRelativePathToFull(Primary),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(TestTrue(TEXT("Primary held without delete sharing"),Lock!=INVALID_HANDLE_VALUE))
 {
  S->Elapsed=3;TestFalse(TEXT("Replace failure is reported"),RTSCampaignStorage::Write(S,Slot,Valid,Error));::CloseHandle(Lock);
  FFileHelper::LoadFileToArray(After,*Primary);TestTrue(TEXT("Failed replacement retains exact primary bytes"),After==Before);
  TestFalse(TEXT("Failed write removes its pending file"),IFileManager::Get().FileExists(*(Primary+TEXT(".pending"))));
 }
#endif
 FFileHelper::LoadFileToArray(BackupBytes,*Previous);FFileHelper::SaveArrayToFile(TArray<uint8>{0,1,2,3},*Primary);S->Elapsed=4;
 TestTrue(TEXT("New save can replace corrupt primary safely"),RTSCampaignStorage::Write(S,Slot,Valid,Error));FFileHelper::LoadFileToArray(After,*Previous);TestTrue(TEXT("Corrupt primary never replaces good backup"),After==BackupBytes);
 TestFalse(TEXT("Path traversal slot rejected"),RTSCampaignStorage::Write(S,TEXT("../NotASlot"),Valid,Error));
 AddInfo(TEXT("STORAGE_RECOVERY_OK verified_write=1 previous=1 sharing_failure=1 corrupt_preserves_backup=1"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignBusyRoundtrip,"MiniRTS.Persistence.BusyEconomyAndFailureIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
namespace
{
 FString BusySlotStorage;const TCHAR* BusySlot=nullptr;
 class FCheckBusyEconomy : public IAutomationLatentCommand
 {
  FAutomationTestBase* T;double Began=FPlatformTime::Seconds(),Phase=0;int32 Stage=0;FVector BuildAt=ARTSCampaign::HomeLocation()+FVector(6000,-6000,0);TWeakObjectPtr<ARTSUnit> Gatherer,Builder;TWeakObjectPtr<ARTSBuilding> Site;TStrongObjectPtr<URTSCampaignSave> Saved;int32 Cargo=0,Wood=0,BeforeSwords=0;float BuildProgress=0,TrainProgress=0;
  bool bEntryHidden=false;
  void Cleanup(){UGameplayStatics::DeleteGameInSlot(BusySlot,0);UGameplayStatics::DeleteGameInSlot(*RTSCampaignStorage::PreviousSlot(BusySlot),0);}
  ARTSBuilding* FindSite(UWorld* W){for(TActorIterator<ARTSBuilding> It(W);It;++It)if(It->IsAlive() && It->GetActorLocation().Equals(BuildAt,1))return *It;return nullptr;}
  int32 SwordCount(UWorld* W){int32 N=0;for(TActorIterator<ARTSUnit> It(W);It;++It)N+=It->IsAlive() && It->GetTeam()==0 && It->GetUnitData()->BalanceId==TEXT("Swordman");return N;}
 public:
  explicit FCheckBusyEconomy(FAutomationTestBase* Test):T(Test){}
  bool Update() override
  {
   UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;auto* C=W?ARTSCampaign::Get(W):nullptr;
   if(FPlatformTime::Seconds()-Began>180){T->AddError(FString::Printf(TEXT("Busy persistence timeout stage=%d msg=%s"),Stage,C?*C->GetMessage():TEXT("no world")));Cleanup();return true;}
   if(W && Stage==0)
   {
    TArray<UUserWidget*> Entries;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Entries,URTSCampaignEntryWidget::StaticClass(),true);
    for(auto* Entry:Entries)if(Entry->IsInViewport()){Entry->SetVisibility(ESlateVisibility::Hidden);bEntryHidden=true;return false;}
   }
   if(!C || !C->IsReady())return false;
   auto* Balance=URTSBalanceSubsystem::Get(W);auto* Stock=Balance->GetResources(0);auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());const double Now=W->GetTimeSeconds();
   if(Stage==0)
   {
    if(!T->TestTrue(TEXT("Startup progresses while loading widget is not painted"),bEntryHidden))return true;
    T->AddInfo(TEXT("ENTRY_READY_WITHOUT_SLATE hidden_loading_widget=1"));
    FRTSCampaignPersistenceTestAccess::Prepare(C);Stock->SetAmounts({{ERTSResource::Wood,3000},{ERTSResource::Food,3000}});
    if(!FRTSCampaignPersistenceTestAccess::ProducerFixture(C,TEXT("Barracks"))){T->AddError(TEXT("Cannot prepare paid producer fixture"));return true;}
    TArray<ARTSUnit*> Workers;for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0){FRTSOrder Stop;Stop.Type=ERTSOrderType::Stop;It->ReceiveOrder(Stop);if(It->GetUnitData()->bWorker)Workers.Add(*It);}
    if(Workers.Num()<2){T->AddError(TEXT("Missing workers"));return true;}Gatherer=Workers[0];Builder=Workers[1];
    ARTSResourceNode* WoodNode=nullptr;for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(It->GetResourceType()==ERTSResource::Wood && FVector::Dist2D(It->GetActorLocation(),ARTSCampaign::HomeLocation())<2000){WoodNode=*It;break;}
    if(!WoodNode){T->AddError(TEXT("Missing wood node"));return true;}FRTSOrder Gather;Gather.Type=ERTSOrderType::Gather;Gather.TargetActor=WoodNode;Gatherer->ReceiveOrder(Gather);
    Phase=Now;Stage=10;return false;
   }
   if(Stage==10)
   {
    if(!Gatherer.IsValid() || Gatherer->GetGatherer()->GetCarriedAmount()<2)return false;
    FString Error;Site=Balance->PlaceBuilding(TEXT("Barracks"),0,BuildAt,Error);if(!Site.IsValid()){T->AddError(Error);return true;}Site->AddConstructionWork(2);URTSOrderLibrary::IssueWorkOrder({Builder.Get()},Site.Get());
    C->SelectServiceBuilding(TEXT("Barracks"));T->TestTrue(TEXT("First paid recruit"),C->Train(TEXT("Swordman")));T->TestTrue(TEXT("Second paid recruit"),C->Train(TEXT("Swordman")));
    PC->SelectBuilding(nullptr);PC->GetSelection()->SelectSingle(Builder.Get(),false);T->TestTrue(TEXT("Paid facility starts from worker selection"),C->BuyFacility(0));Phase=Now;Stage=1;return false;
   }
   if(Stage==1)
   {
    if(Now-Phase<.35 || !Gatherer.IsValid() || Gatherer->GetGatherer()->GetCarriedAmount()<2)return false;
    auto* Barracks=C->FindServiceBuilding(TEXT("Barracks"));if(!Barracks || Barracks->GetProducer()->GetQueue().Num()!=2){T->AddError(TEXT("Fixture trained before cargo test"));return true;}
    Cargo=Gatherer->GetGatherer()->GetCarriedAmount();Wood=Stock->GetAmount(ERTSResource::Wood);BuildProgress=Site->GetBuildProgress();TrainProgress=Barracks->GetProducer()->GetProgress();BeforeSwords=SwordCount(W);
    T->TestTrue(TEXT("Construction remains unfinished"),BuildProgress>0 && BuildProgress<1);T->TestTrue(TEXT("Head training genuinely progressed"),TrainProgress>0 && TrainProgress<1);T->TestTrue(TEXT("Facility still in progress"),C->GetPendingFacility()==0);
    if(!T->TestTrue(FString(TEXT("Save without stopping cargo/build/training: "))+C->GetMessage(),C->SaveCampaign()))return true;
    FString Error;Saved.Reset(FRTSCampaignPersistenceTestAccess::Capture(C,Error));T->TestNotNull(TEXT("Full schema snapshot"),Saved.Get());if(!Saved)return true;
    T->TestEqual(TEXT("Schema explicitly current"),Saved->Version,URTSCampaignSave::SnapshotVersion);T->TestTrue(TEXT("Busy snapshot validates"),FRTSCampaignPersistenceTestAccess::Validate(C,Saved.Get()));
    auto EconomyBad=TStrongObjectPtr<URTSCampaignSave>(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));EconomyBad->FactionEconomies[0].Stock.Add(ERTSResource::Food,-1);T->TestFalse(TEXT("Negative faction stock rejected"),FRTSCampaignPersistenceTestAccess::Validate(C,EconomyBad.Get()));
    EconomyBad.Reset(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));EconomyBad->BuildingFactions.Pop();T->TestFalse(TEXT("Parallel faction attribution mismatch rejected"),FRTSCampaignPersistenceTestAccess::Validate(C,EconomyBad.Get()));
    auto FloatingNode=TStrongObjectPtr<URTSCampaignSave>(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));FloatingNode->EnemyNodes[0].Feet.Z+=400;
    FString NodeError;T->TestFalse(TEXT("Floating enemy resource rejected without mutation"),FRTSCampaignPersistenceTestAccess::Restore(C,FloatingNode.Get(),NodeError));T->TestTrue(TEXT("Node ground rejection preserves live worker"),Gatherer.IsValid() && Gatherer->IsAlive());T->TestEqual(TEXT("Node ground rejection preserves stock"),Stock->GetAmount(ERTSResource::Wood),Wood);
    auto Bad=TStrongObjectPtr<URTSCampaignSave>(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));int32 WorkerIndex=INDEX_NONE;for(int32 I=0;I<Bad->Units.Num();++I)if(Bad->Units[I].OrderType==ERTSOrderType::Gather){WorkerIndex=I;break;}
    if(WorkerIndex==INDEX_NONE){T->AddError(TEXT("Gather order not captured"));return true;}Bad->Units[WorkerIndex].TargetIndex=999999;T->TestFalse(TEXT("Dangling gather target rejected before mutation"),FRTSCampaignPersistenceTestAccess::Validate(C,Bad.Get()));
    auto Blocked=TStrongObjectPtr<URTSCampaignSave>(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));Blocked->Units[0].Feet=FVector(3500,0,-250);T->TestFalse(TEXT("Unnavigable snapshot rejected"),FRTSCampaignPersistenceTestAccess::Restore(C,Blocked.Get(),Error));T->TestTrue(TEXT("Failed preflight keeps original worker"),Gatherer.IsValid() && Gatherer->IsAlive());T->TestEqual(TEXT("Failed preflight keeps stock"),Stock->GetAmount(ERTSResource::Wood),Wood);
    // Real staged reconstruction failure after valid schema/nav, not a synthetic boolean.
    auto* Obstacle=W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Obstacle);Obstacle->SetRootComponent(Box);Box->SetBoxExtent(FVector(120));Box->SetCollisionObjectType(ECC_WorldDynamic);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Box->SetCanEverAffectNavigation(false);Box->RegisterComponent();Obstacle->SetActorLocation(Saved->Units[0].Feet+FVector(0,0,100));
    T->TestFalse(TEXT("Blocking world object rejects staged reconstruction"),FRTSCampaignPersistenceTestAccess::Restore(C,Saved.Get(),Error));T->TestTrue(TEXT("Staged failure keeps original worker alive"),Gatherer.IsValid() && Gatherer->IsAlive());T->TestTrue(TEXT("Staged failure keeps original site"),Site.IsValid() && Site->IsAlive());T->TestEqual(TEXT("Staged failure preserves stock"),Stock->GetAmount(ERTSResource::Wood),Wood);Obstacle->SetActorEnableCollision(false);Obstacle->Destroy();
    auto Overlap=TStrongObjectPtr<URTSCampaignSave>(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));
    const int32 SiteIndex=Overlap->BuildingPositions.IndexOfByPredicate([&](const FVector& P){return P.Equals(BuildAt,1);});
    if(!T->TestTrue(TEXT("Saved construction site found"),SiteIndex>=0))return true;
    const FName DuplicateKind=Overlap->BuildingKinds[SiteIndex];const FVector DuplicatePosition=Overlap->BuildingPositions[SiteIndex];const float DuplicateHealth=Overlap->BuildingHealth[SiteIndex];auto DuplicateWork=Overlap->BuildingWork[SiteIndex];DuplicateWork.Facing=45;
    Overlap->BuildingKinds.Add(DuplicateKind);Overlap->BuildingPositions.Add(DuplicatePosition);Overlap->BuildingHealth.Add(DuplicateHealth);Overlap->BuildingWork.Add(DuplicateWork);
    Overlap->BuildingTeams.Add(0);Overlap->BuildingFactions.Add(-1);
    T->TestTrue(TEXT("Overlapping building fixture passes schema"),FRTSCampaignPersistenceTestAccess::Validate(C,Overlap.Get()));
    T->TestFalse(TEXT("Intersecting staged buildings rejected"),FRTSCampaignPersistenceTestAccess::Restore(C,Overlap.Get(),Error));T->TestTrue(TEXT("Pairwise building collision reports its reason"),Error.Contains(TEXT("建筑占位冲突")));
    T->TestTrue(TEXT("Building overlap keeps original worker and site"),Gatherer.IsValid() && Gatherer->IsAlive() && Site.IsValid() && Site->IsAlive());T->TestEqual(TEXT("Building overlap preserves stock"),Stock->GetAmount(ERTSResource::Wood),Wood);
    T->AddInfo(TEXT("STAGED_BUILDING_OVERLAP_REJECTED world_preserved=1 stock_preserved=1"));
    auto Extra=TStrongObjectPtr<URTSCampaignSave>(DuplicateObject<URTSCampaignSave>(Saved.Get(),GetTransientPackage()));
    FRTSCampaignBuildingWorkSave Farm;Farm.bHasFoodSource=true;Farm.FoodFeet=ARTSCampaign::HomeLocation()+FVector(4000,-5300,0);Farm.FoodRemaining=137;Farm.FoodRemainder=.375f;
    const int32 FarmIndex=Extra->BuildingKinds.Num();Extra->BuildingKinds.Add(TEXT("Farm"));Extra->BuildingPositions.Add(ARTSCampaign::HomeLocation()+FVector(4000,-6000,0));Extra->BuildingHealth.Add(1);Extra->BuildingWork.Add(Farm);
    Extra->BuildingTeams.Add(0);Extra->BuildingFactions.Add(-1);
    // Their world-axis bounds intersect, but the yaw-aligned collision boxes do not.
    const FVector PairAt=ARTSCampaign::HomeLocation()+FVector(5000,-6500,0);const FRotator PairFacing(0,45,0);
    for(int32 I=0;I<2;++I){FRTSCampaignBuildingWorkSave Work;Work.Progress=.25f;Work.Facing=45;Extra->BuildingKinds.Add(TEXT("Barracks"));Extra->BuildingPositions.Add(PairAt+PairFacing.RotateVector(FVector(0,I*200,0)));Extra->BuildingHealth.Add(1);Extra->BuildingWork.Add(Work);}
    for(int32 I=0;I<2;++I){Extra->BuildingTeams.Add(0);Extra->BuildingFactions.Add(-1);}
    Extra->NodeCapture[0].Seconds=1.75f;Extra->NodeCapture[0].Team=Extra->NodeOwners[0]==0?1:0;
    int32 Moving=0;for(auto& U:Extra->Units)if(U.Team==0 && U.Kind==TEXT("Swordman") && Moving<2){U.OrderType=ERTSOrderType::Move;U.MoveGroup=0;U.MoveGoal=ARTSCampaign::HomeLocation()+FVector(5000,1500,0);U.OrderLocation=U.MoveGoal;++Moving;}
    for(auto& U:Extra->Units)if(U.Team==0 && U.Kind==TEXT("Peasant") && U.OrderType==ERTSOrderType::Stop){U.OrderType=ERTSOrderType::Gather;U.TargetClass=5;U.TargetIndex=FarmIndex;break;}
    const bool ExtraRestored=FRTSCampaignPersistenceTestAccess::Restore(C,Extra.Get(),Error);T->TestTrue(FString(TEXT("Farm/capture/group staging roundtrip: "))+Error,ExtraRestored);
    if(ExtraRestored){auto* Restored=FRTSCampaignPersistenceTestAccess::Capture(C,Error);const int32 At=Restored?Restored->BuildingKinds.IndexOfByKey(FName("Farm")):INDEX_NONE;T->TestTrue(TEXT("Farm resource not refilled"),At>=0 && Restored->BuildingWork[At].FoodRemaining==137 && FMath::IsNearlyEqual(Restored->BuildingWork[At].FoodRemainder,.375f));T->TestTrue(TEXT("Resource capture fraction retained"),Restored && FMath::IsNearlyEqual(Restored->NodeCapture[0].Seconds,1.75f));int32 M=0;if(Restored)for(const auto& U:Restored->Units)M+=U.Team==0 && U.MoveGroup>=0 && U.MoveGoal.Equals(ARTSCampaign::HomeLocation()+FVector(5000,1500,0),.01);T->TestEqual(TEXT("Both group members retain raw goal"),M,2);}
    if(ExtraRestored)T->AddInfo(TEXT("STAGED_ROTATED_BUILDINGS_ACCEPTED separated_boxes=1 intersecting_aabbs=1"));
    Stock->Add(ERTSResource::Wood,13);if(!T->TestTrue(FString(TEXT("Busy load succeeds: "))+C->GetMessage(),C->LoadCampaign()))return true;
    int32 RestoredNodes=0;
    for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(!It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")))
    {
     auto* Root=Cast<UPrimitiveComponent>(It->GetRootComponent());
     T->TestTrue(TEXT("Restored resource preserves physical blocking and navigation footprint"),Root && Root->CanEverAffectNavigation() && Root->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block && It->GetActorEnableCollision());
     ++RestoredNodes;
    }
    T->TestTrue(TEXT("All shared and faction resources restored for nav regression"),RestoredNodes>=40);
    T->TestEqual(TEXT("No spend or deposit during load"),Stock->GetAmount(ERTSResource::Wood),Wood);Site=FindSite(W);T->TestTrue(TEXT("Exact construction retained"),Site.IsValid() && FMath::IsNearlyEqual(Site->GetBuildProgress(),BuildProgress,.0001f));
    Barracks=C->FindServiceBuilding(TEXT("Barracks"));T->TestTrue(TEXT("Paid FIFO and fractional head restored"),Barracks && Barracks->GetProducer()->GetQueue().Num()==2 && FMath::IsNearlyEqual(Barracks->GetProducer()->GetProgress(),TrainProgress,.0001f));
    Gatherer.Reset();Builder.Reset();for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0){if(It->GetCurrentOrder().Type==ERTSOrderType::Gather)Gatherer=*It;if(It->GetCurrentOrder().Type==ERTSOrderType::Build)Builder=*It;}
    T->TestTrue(TEXT("Cargo and resource target restored"),Gatherer.IsValid() && Gatherer->GetGatherer()->GetCarriedAmount()==Cargo && Cast<ARTSResourceNode>(Gatherer->GetCurrentOrder().TargetActor.Get()));T->TestTrue(TEXT("Builder target resolves to restored site"),Builder.IsValid() && Builder->GetCurrentOrder().TargetActor.Get()==Site.Get());
    // Save again while busy: rotation must preserve the previous valid primary.
    T->TestTrue(TEXT("Second busy save"),C->SaveCampaign());T->TestTrue(TEXT("Previous valid file retained"),ARTSCampaign::HasPreviousCampaign());
    auto* AfterReload=C->FindServiceBuilding(TEXT("Barracks"));const int32 FoodBefore=Stock->GetAmount(ERTSResource::Food);const auto Refund=Saved->BuildingWork[Saved->BuildingKinds.IndexOfByKey(FName("Barracks"))].Production.Orders.Last().Cost;
    T->TestTrue(TEXT("Cancel restored tail"),AfterReload->GetProducer()->CancelLast());T->TestEqual(TEXT("Restored order refunds original wood exactly once"),Stock->GetAmount(ERTSResource::Wood),Wood+Refund.FindRef(ERTSResource::Wood));T->TestEqual(TEXT("Restored order refunds original food exactly once"),Stock->GetAmount(ERTSResource::Food),FoodBefore+Refund.FindRef(ERTSResource::Food));
    if(!T->TestTrue(TEXT("Repeat load restores without duplication"),C->LoadCampaign()))return true;T->TestEqual(TEXT("Repeat load resets stock, no duplicate refund"),Stock->GetAmount(ERTSResource::Wood),Wood);T->TestEqual(TEXT("Repeat load same living soldier count"),SwordCount(W),BeforeSwords);
    const FString Primary=FPaths::ProjectSavedDir()/TEXT("SaveGames")/(FString(BusySlot)+TEXT(".sav"));TArray<uint8> Bytes;FFileHelper::LoadFileToArray(Bytes,*Primary);const TArray<uint8> Broken={0,1,2,3};FFileHelper::SaveArrayToFile(Broken,*Primary);auto* OriginalSite=FindSite(W);
    T->TestFalse(TEXT("Corrupt current file not hidden by backup"),C->LoadCampaign());T->TestTrue(TEXT("Corrupt load retains current world"),OriginalSite && OriginalSite->IsAlive());T->TestTrue(TEXT("Explicit previous recovery"),C->LoadPreviousCampaign());FFileHelper::SaveArrayToFile(Bytes,*Primary);
    Site=FindSite(W);Phase=Now;Stage=2;return false;
   }
   if(Stage==2)
   {
    bool Delivered=Stock->GetAmount(ERTSResource::Wood)>Wood;const bool Built=Site.IsValid() && Site->IsComplete();const bool Trained=SwordCount(W)>=BeforeSwords+2;const bool Facility=C->GetPendingFacility()<0;
    if(!(Delivered && Built && Trained && Facility))return false;
    T->TestTrue(TEXT("Actual worker delivers after restore"),Delivered);T->TestTrue(TEXT("Actual worker finishes saved site"),Built);T->TestEqual(TEXT("Exactly two paid recruits finish"),SwordCount(W),BeforeSwords+2);T->TestTrue(TEXT("Facility timer completes without repaying"),Facility);
    FString Error;auto* Complete=FRTSCampaignPersistenceTestAccess::Capture(C,Error);const bool Valid=Complete && FRTSCampaignPersistenceTestAccess::Validate(C,Complete);if(!Valid && Complete){TArray<uint8> Diagnostic;RTSCampaignStorage::Encode(Complete,Diagnostic);FFileHelper::SaveArrayToFile(Diagnostic,*(FPaths::ProjectSavedDir()/TEXT("Automation/BusySave-InvalidSnapshot.sav")));}T->TestTrue(FString(TEXT("Post-resume snapshot valid: "))+Error,Valid);
    T->AddInfo(FString::Printf(TEXT("BUSY_ECONOMY_RESUMED cargo=%d build=%.3f train=%.3f delivered=1 built=1 recruits=2 facility=1 failure_isolation=1"),Cargo,BuildProgress,TrainProgress));Cleanup();return true;
   }
   return false;
  }
 };
}
bool FRTSCampaignBusyRoundtrip::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("BusyEconomy"));BusySlotStorage=Scope->Slot;BusySlot=*BusySlotStorage;
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckBusyEconomy(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
namespace
{
 FString ControlSlotStorage;const TCHAR* ControlSlot=nullptr;
 class FCheckControlsRallyAutosave : public IAutomationLatentCommand
 {
  FAutomationTestBase* T;double Began=FPlatformTime::Seconds(),TrainStart=0;int32 Stage=0,BeforeSword=0;
  TWeakObjectPtr<ARTSUnit> Grouped;TWeakObjectPtr<ARTSBuilding> Barracks;FVector RallyAt=FVector::ZeroVector;
  int32 SwordCount(UWorld* W){int32 N=0;for(TActorIterator<ARTSUnit> It(W);It;++It)N+=It->IsAlive() && It->GetTeam()==0 && It->GetUnitData() && It->GetUnitData()->BalanceId==TEXT("Swordman");return N;}
 public:
  explicit FCheckControlsRallyAutosave(FAutomationTestBase* Test):T(Test){}
  bool Update() override
  {
   UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;auto* C=W?ARTSCampaign::Get(W):nullptr;
   if(FPlatformTime::Seconds()-Began>150){T->AddError(FString::Printf(TEXT("Controls/rally/autosave timeout stage=%d"),Stage));return true;}
   if(!C || !C->IsReady())return false;
   auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());if(!PC){T->AddError(TEXT("Missing controller"));return true;}
   auto* Sel=PC->GetSelection();if(!Sel){T->AddError(TEXT("Missing selection"));return true;}
   if(Stage==0)
   {
    if(!FRTSCampaignPersistenceTestAccess::ProducerFixture(C,TEXT("Barracks"))){T->AddError(TEXT("Cannot prepare paid rally producer"));return true;}
    FRTSCampaignPersistenceTestAccess::Prepare(C);
    TArray<ARTSUnit*> Mil;for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData() && !It->GetUnitData()->bWorker)Mil.Add(*It);
    if(Mil.Num()<3){T->AddError(TEXT("Need three combat units"));return true;}
    Sel->ClearSelection();for(int32 I=0;I<2;++I)Sel->SelectSingle(Mil[I],true);
    PC->AssignControlGroup(0);T->TestEqual(TEXT("Control group stores the selection"),PC->GetControlGroupSize(0),2);
    Sel->ClearSelection();T->TestTrue(TEXT("Recall replaces the selection"),PC->RecallControlGroup(0,false) && Sel->GetSelectedCount()==2);
    Sel->ClearSelection();Sel->SelectSingle(Mil[2],false);
    T->TestTrue(TEXT("Shift recall adds to an existing selection"),PC->RecallControlGroup(0,true) && Sel->GetSelectedCount()==3);
    Grouped=Mil[0];
    int32 Idle=0;for(TActorIterator<ARTSUnit> It(W);It;++It){if(!It->IsAlive() || It->GetTeam()!=0 || !It->GetUnitData() || !It->GetUnitData()->bWorker)continue;const ERTSOrderType Ty=It->GetCurrentOrder().Type;if(Ty==ERTSOrderType::None || Ty==ERTSOrderType::Stop)++Idle;}
    T->TestEqual(TEXT("Overview idle-worker feedback matches the world"),C->GetIdleWorkerCount(),Idle);
    T->TestTrue(TEXT("Idle-worker hotkey selects a worker"),PC->SelectIdleWorker());
    T->TestTrue(TEXT("All-military hotkey returns the army"),PC->SelectAllMilitary()>=3);
    T->TestEqual(TEXT("Blocked production feedback is zero while idle"),C->GetBlockedProductionCount(),0);
    Stage=1;return false;
   }
   if(Stage==1)
   {
    if(Grouped.IsValid() && Grouped->IsAlive())Grouped->GetHealth()->ApplyDamage(1000000.f,nullptr);
    if(Grouped.IsValid() && Grouped->IsAlive())return false;
    Sel->ClearSelection();const bool bRecall=PC->RecallControlGroup(0,false);
    T->TestTrue(TEXT("Recall silently drops a destroyed member"),bRecall && Sel->GetSelectedCount()>=1 && Sel->GetSelectedCount()<=2);
    Stage=2;return false;
   }
   if(Stage==2)
   {
    Barracks=C->FindServiceBuilding(TEXT("Barracks"));
    if(!Barracks.IsValid()){T->AddError(TEXT("Campaign has no completed Barracks"));return true;}
    RallyAt=ARTSCampaign::HomeLocation()+FVector(4000,-6000,0);
    PC->SelectBuilding(Barracks.Get());
    T->TestTrue(TEXT("Rally point accepted on a completed building"),PC->SetSelectedBuildingRally(RallyAt));
    T->TestTrue(TEXT("Rally point is stored on the building"),Barracks.IsValid() && Barracks->HasRallyPoint());
    C->SelectServiceBuilding(TEXT("Barracks"));BeforeSword=SwordCount(W);
    T->TestTrue(FString(TEXT("Paid training beside a rally point: "))+C->GetMessage(),C->Train(TEXT("Swordman")));
    TrainStart=FPlatformTime::Seconds();Stage=3;return false;
   }
   if(Stage==3)
   {
    if(SwordCount(W)<BeforeSword+1){if(FPlatformTime::Seconds()-TrainStart>30){T->AddError(TEXT("Rally training timeout"));return true;}return false;}
    bool bRally=false;
    for(TActorIterator<ARTSUnit> It(W);It;++It)
    {
     if(!It->IsAlive() || It->GetTeam()!=0 || !It->GetUnitData() || It->GetUnitData()->BalanceId!=TEXT("Swordman"))continue;
     const FRTSOrder& Order=It->GetCurrentOrder();
     if(Order.Type==ERTSOrderType::Move && FVector::Dist2D(Order.TargetLocation,RallyAt)<400)bRally=true;
    }
    T->TestTrue(TEXT("New unit exits toward the player rally point"),bRally);
    Stage=4;return false;
   }
   if(Stage==4)
   {
    ARTSCampaign::SetSaveSlotOverride(ControlSlot);
    UGameplayStatics::DeleteGameInSlot(ControlSlot,0);UGameplayStatics::DeleteGameInSlot(*RTSCampaignStorage::PreviousSlot(ControlSlot),0);
    UGameplayStatics::DeleteGameInSlot(*ARTSCampaign::AutosaveSlotName(),0);UGameplayStatics::DeleteGameInSlot(*RTSCampaignStorage::PreviousSlot(ARTSCampaign::AutosaveSlotName()),0);
    T->TestFalse(TEXT("No manual save exists yet"),UGameplayStatics::DoesSaveGameExist(*ARTSCampaign::SaveSlotName(),0));
    T->TestTrue(TEXT("Automatic safe point writes its own slot"),C->SaveAutosave() && ARTSCampaign::HasAutosave());
    T->TestFalse(TEXT("Autosave never creates the manual slot"),UGameplayStatics::DoesSaveGameExist(*ARTSCampaign::SaveSlotName(),0));
    T->TestTrue(TEXT("Manual save still works beside the autosave"),C->SaveCampaign());
    T->TestTrue(TEXT("Autosave record survives a manual save"),ARTSCampaign::HasAutosave());
    T->TestTrue(TEXT("Explicit autosave load succeeds"),C->LoadAutosave());
    // P0-05 extras: extra manual slots never share a name with the primary or the autosave.
    T->TestEqual(TEXT("Slot 0 stays the primary manual slot"),ARTSCampaign::SlotNameFor(0),ARTSCampaign::SaveSlotName());
    T->TestEqual(TEXT("Slot 2 keeps a dedicated name"),ARTSCampaign::SlotNameFor(1),ARTSCampaign::SaveSlotName()+TEXT("_alt2"));
    T->TestFalse(TEXT("Slot 2 starts empty"),ARTSCampaign::HasSlot(1));
    T->TestTrue(TEXT("Save to slot 2 succeeds"),C->SaveToSlot(1));
    T->TestTrue(TEXT("Slot 2 record exists"),ARTSCampaign::HasSlot(1));
    T->TestTrue(TEXT("Primary record survives an extra slot save"),UGameplayStatics::DoesSaveGameExist(*ARTSCampaign::SaveSlotName(),0));
    T->TestTrue(TEXT("Autosave survives an extra slot save"),ARTSCampaign::HasAutosave());
    const double BeforeSlotLoad=C->GetElapsed();
    T->TestTrue(TEXT("Load from slot 2 succeeds"),C->LoadFromSlot(1));
    T->TestTrue(TEXT("Slot 2 restore keeps campaign time"),FMath::Abs(C->GetElapsed()-BeforeSlotLoad)<120.0);
    // P1-02: the production hotkey must land on an owned, completed producer and never on an enemy building.
    if(PC->SelectNextProductionBuilding())
    {
     ARTSBuilding* Focused=PC->GetSelectedBuilding();
     T->TestTrue(TEXT("Production hotkey selects an owned completed producer"),Focused && Focused->GetTeam()==0 && Focused->IsComplete() && Focused->IsAlive());
    }
    else T->TestFalse(TEXT("Production hotkey finds the campaign barracks"),true);
    // P0-04: no settlement card while the campaign is still running normally.
    T->TestFalse(TEXT("Running campaign reports no settlement state"),C->IsCompleted() || C->IsDefeated());
    UGameplayStatics::DeleteGameInSlot(ControlSlot,0);UGameplayStatics::DeleteGameInSlot(*RTSCampaignStorage::PreviousSlot(ControlSlot),0);
    UGameplayStatics::DeleteGameInSlot(*ARTSCampaign::AutosaveSlotName(),0);UGameplayStatics::DeleteGameInSlot(*RTSCampaignStorage::PreviousSlot(ARTSCampaign::AutosaveSlotName()),0);
    UGameplayStatics::DeleteGameInSlot(*ARTSCampaign::SlotNameFor(1),0);
    T->AddInfo(TEXT("CONTROLS_RALLY_AUTOSAVE_OK groups=1 rally=1 autosave=1 feedback=1 slots=1 hotkey=1 settlement=1"));
    return true;
   }
   return false;
  }
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSControlsRallyAutosave,"MiniRTS.Controls.ControlGroupsRallyAutosave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSControlsRallyAutosave::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("Controls"));ControlSlotStorage=Scope->Slot;ControlSlot=*ControlSlotStorage;
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckControlsRallyAutosave(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
namespace
{
 FString QueueSlotStorage;const TCHAR* QueueSlot=nullptr;
 /**
  * P1-03: queued steps must be real order state. They run only after the active order ends,
  * an explicit stop cancels everything, and attack-move remembers its destination.
  */
 class FCheckOrderQueueAndAttackMove : public IAutomationLatentCommand
 {
  FAutomationTestBase* T;double Began=FPlatformTime::Seconds(),MarchStart=0;int32 Stage=0;
  TArray<TWeakObjectPtr<ARTSUnit>> Units;FVector FirstGoal=FVector::ZeroVector,SecondGoal=FVector::ZeroVector;
 public:
  explicit FCheckOrderQueueAndAttackMove(FAutomationTestBase* Test):T(Test){}
  bool Update() override
  {
   UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;auto* C=W?ARTSCampaign::Get(W):nullptr;
   if(FPlatformTime::Seconds()-Began>180){T->AddError(FString::Printf(TEXT("Order queue/attack-move timeout stage=%d"),Stage));return true;}
   if(!C || !C->IsReady())return false;
   auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());if(!PC){T->AddError(TEXT("Missing controller"));return true;}
   auto* Sel=PC->GetSelection();if(!Sel){T->AddError(TEXT("Missing selection"));return true;}
   if(Stage==0)
   {
    FRTSCampaignPersistenceTestAccess::Prepare(C);
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData() && !It->GetUnitData()->bWorker)Units.Add(*It);
    if(Units.Num()<2){T->AddError(TEXT("Need two combat units"));return true;}
    Units.SetNum(2);
    Sel->ClearSelection();for(const auto& U:Units)if(U.IsValid())Sel->SelectSingle(U.Get(),true);
    FVector Centroid=FVector::ZeroVector;for(const auto& U:Units)if(U.IsValid())Centroid+=U->GetActorLocation();Centroid/=Units.Num();
    FirstGoal=Centroid+FVector(400,0,0);SecondGoal=FirstGoal+FVector(350,0,0);
    if(UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
    {
     FNavLocation P;
     if(Nav->ProjectPointToNavigation(FirstGoal,P,FVector(300,300,400)))FirstGoal=P.Location;
     if(Nav->ProjectPointToNavigation(SecondGoal,P,FVector(300,300,400)))SecondGoal=P.Location;
    }
    TArray<ARTSUnit*> Live;for(const auto& U:Units)if(U.IsValid())Live.Add(U.Get());
    URTSOrderLibrary::IssueMoveOrder(W,Live,FirstGoal,false);
    T->TestEqual(TEXT("Plain order issues without a backlog"),Live[0]->GetQueuedOrderCount(),0);
    URTSOrderLibrary::IssueMoveOrder(W,Live,SecondGoal,true);
    int32 Queued=0;for(ARTSUnit* U:Live)Queued+=U->GetQueuedOrderCount();
    T->TestEqual(TEXT("Shift queue stores one extra step per unit"),Queued,Live.Num());
    URTSOrderLibrary::IssueMoveOrder(W,Live,FirstGoal,false);
    int32 After=0;for(ARTSUnit* U:Live)After+=U->GetQueuedOrderCount();
    T->TestEqual(TEXT("A new plain order clears the queue"),After,0);
    Stage=1;return false;
   }
   if(Stage==1)
   {
    TArray<ARTSUnit*> Live;for(const auto& U:Units)if(U.IsValid() && U->IsAlive())Live.Add(U.Get());
    if(Live.Num()<2){T->AddError(TEXT("Units lost before the march"));return true;}
    URTSOrderLibrary::IssueMoveOrder(W,Live,FirstGoal,false);
    URTSOrderLibrary::IssueMoveOrder(W,Live,SecondGoal,true);
    int32 Queued=0;for(ARTSUnit* U:Live)Queued+=U->GetQueuedOrderCount();
    T->TestEqual(TEXT("March carries one queued step"),Queued,Live.Num());
    MarchStart=FPlatformTime::Seconds();Stage=2;return false;
   }
   if(Stage==2)
   {
    int32 Advanced=0,Live=0;
    for(const auto& U:Units)if(U.IsValid() && U->IsAlive())
    {
     ++Live;const FRTSOrder& Order=U->GetCurrentOrder();
     if(Order.Type==ERTSOrderType::Move && FVector::Dist2D(Order.TargetLocation,SecondGoal)<60)++Advanced;
    }
    if(Live>0 && Advanced==Live){T->TestTrue(TEXT("Queued step becomes the active order after the first arrival"),true);Stage=3;return false;}
    if(FPlatformTime::Seconds()-MarchStart>40){T->AddError(FString::Printf(TEXT("Queued march did not advance: %d of %d"),Advanced,Live));return true;}
    return false;
   }
   if(Stage==3)
   {
    TArray<ARTSUnit*> Live;for(const auto& U:Units)if(U.IsValid() && U->IsAlive())Live.Add(U.Get());
    if(Live.Num()<2){T->AddError(TEXT("Units lost before the attack-move check"));return true;}
    URTSOrderLibrary::IssueStopOrder(Live);
    int32 Queued=0;bool bMarching=false;
    for(ARTSUnit* U:Live){Queued+=U->GetQueuedOrderCount();bMarching|=U->GetCurrentOrder().Type==ERTSOrderType::AttackMove || U->HasAttackMoveGoal();}
    T->TestEqual(TEXT("Stop clears queued steps"),Queued,0);
    T->TestFalse(TEXT("Stop also ends attack-move and forgets its goal"),bMarching);
    const FVector March=SecondGoal+FVector(300,0,0);
    Sel->ClearSelection();for(ARTSUnit* U:Live)Sel->SelectSingle(U,true);
    T->TestFalse(TEXT("Removed player attack-move is rejected"),PC->RequestAttackMoveAt(March));
    bool bType=false,bGoal=false;
    for(ARTSUnit* U:Live)
    {
     bType|=U->GetCurrentOrder().Type==ERTSOrderType::AttackMove;
     bGoal|=U->HasAttackMoveGoal();
    }
    T->TestFalse(TEXT("Player request does not issue attack-move"),bType);
    T->TestFalse(TEXT("Player request does not remember a march goal"),bGoal);
    T->TestTrue(TEXT("Minimap accepts a walkable move destination"),PC->RequestMinimapMoveAt(March,false));
    bool bAllMove=true;for(ARTSUnit* U:Live)bAllMove&=U->GetCurrentOrder().Type==ERTSOrderType::Move;
    T->TestTrue(TEXT("Minimap issues ordinary movement, not attack-move"),bAllMove);
    T->TestTrue(TEXT("Shift minimap move appends"),PC->RequestMinimapMoveAt(March+FVector(200,0,0),true));
    bool bAllQueued=true;for(ARTSUnit* U:Live)bAllQueued&=U->GetQueuedOrderCount()==1;
    T->TestTrue(TEXT("Minimap queue preserves one following step"),bAllQueued);
    UGameplayStatics::SetGamePaused(W,true);
    T->TestFalse(TEXT("Paused minimap cannot issue orders"),PC->RequestMinimapMoveAt(March,false));
    UGameplayStatics::SetGamePaused(W,false);
    URTSOrderLibrary::IssueStopOrder(Live);
    bool bStillGoal=false;for(ARTSUnit* U:Live)bStillGoal|=U->HasAttackMoveGoal();
    T->TestFalse(TEXT("Stop forgets the march goal"),bStillGoal);
    T->AddInfo(TEXT("ORDER_QUEUE_PLAYER_MARCH_REMOVED_OK queue=1 clear=1 removed=1"));
    return true;
   }
   return false;
  }
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSOrderQueueAttackMove,"MiniRTS.Controls.OrderQueueAndAttackMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSOrderQueueAttackMove::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("OrderQueue"));QueueSlotStorage=Scope->Slot;QueueSlot=*QueueSlotStorage;
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckOrderQueueAndAttackMove(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
namespace
{
 const TCHAR* CleanSlotBase=TEXT("MiniRTS_Cleanup_Automation");
}
/**
 * P0-05 start-menu cleanup: a right-clicked row deletes exactly that slot chain and nothing else.
 * The test stages plain slot files, so it never depends on a live campaign or on the production save.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSlotCleanup,"MiniRTS.Persistence.SlotCleanupDeletesOnlyItsChain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSSlotCleanup::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("Cleanup"));
 const FString Base=ARTSCampaign::SaveSlotName();
 const FString Auto=ARTSCampaign::AutosaveSlotName();
 const FString Alt2=ARTSCampaign::SlotNameFor(1);
 const FString Alt3=ARTSCampaign::SlotNameFor(2);
 // Day-start rollback records belong to their own slot chain, so they are staged and checked too.
 const FString Day1=Base+TEXT("_day");
 const FString Day2=Alt2+TEXT("_day");
 TArray<FString> Files={Base,RTSCampaignStorage::PreviousSlot(Base),Auto,RTSCampaignStorage::PreviousSlot(Auto),Alt2,RTSCampaignStorage::PreviousSlot(Alt2),Day1,Day2};
 for(const FString& Slot:Files){ UGameplayStatics::DeleteGameInSlot(Slot,0); }
 // The production record must be irrelevant here: remember its state to prove the test never touches it.
 const FString Production=TEXT("MiniRTS_BorderFactions_v5");
 const bool bProductionBefore=UGameplayStatics::DoesSaveGameExist(Production,0);
 const int64 ProductionSizeBefore=bProductionBefore?IFileManager::Get().FileSize(*RTSCampaignStorage::SlotPath(Production)):-1;
 for(const FString& Slot:Files)
 {
  UGameplayStatics::SaveGameToSlot(UGameplayStatics::CreateSaveGameObject(URTSCampaignSave::StaticClass()),Slot,0);
 }
 bool bStaged=true;
 for(const FString& Slot:Files) bStaged&=UGameplayStatics::DoesSaveGameExist(Slot,0);
 TestTrue(TEXT("All staged slot files exist"),bStaged);
 if(!bStaged)return false;
 TestTrue(TEXT("Menu sees the main record"),ARTSCampaign::HasSlot(0));
 TestTrue(TEXT("Menu sees the autosave record"),ARTSCampaign::HasAutosave());
 ARTSCampaign::FRTSSlotInfo Info;
 TestTrue(TEXT("Slot description reads size and time"),ARTSCampaign::DescribeSlot(1,Info) && Info.bExists && Info.Size>0);
 TestTrue(TEXT("Slot description is honest about an empty slot"),ARTSCampaign::DescribeSlot(2,Info) && !Info.bExists);
 FString Summary;
 TestTrue(*FString::Printf(TEXT("Cleaning slot 2 deletes its chain: %s"),*Summary),ARTSCampaign::DeleteSlotRecords(1,Summary));
 TestFalse(TEXT("Slot 2 record is gone"),ARTSCampaign::HasSlot(1));
 TestFalse(TEXT("Slot 2 backup is gone"),UGameplayStatics::DoesSaveGameExist(*RTSCampaignStorage::PreviousSlot(Alt2),0));
 TestFalse(TEXT("Slot 2 day-start record is gone"),UGameplayStatics::DoesSaveGameExist(*Day2,0));
 TestTrue(TEXT("Main record survives a slot-2 cleanup"),ARTSCampaign::HasSlot(0));
 TestTrue(TEXT("Main backup survives a slot-2 cleanup"),UGameplayStatics::DoesSaveGameExist(*RTSCampaignStorage::PreviousSlot(Base),0));
 TestTrue(TEXT("Main day-start record survives a slot-2 cleanup"),UGameplayStatics::DoesSaveGameExist(*Day1,0));
 TestTrue(TEXT("Autosave survives a slot-2 cleanup"),ARTSCampaign::HasAutosave());
 // Cleaning the main chain also removes its autosave, so a hidden fallback can never re-open the run.
 TestTrue(TEXT("Cleaning the main chain succeeds"),ARTSCampaign::DeleteSlotRecords(0,Summary));
 TestFalse(TEXT("Main record is gone"),ARTSCampaign::HasSlot(0));
 TestFalse(TEXT("Main day-start record is gone"),UGameplayStatics::DoesSaveGameExist(*Day1,0));
 TestFalse(TEXT("Autosave is gone with the main chain"),ARTSCampaign::HasAutosave());
 TestFalse(TEXT("Main backup is gone"),UGameplayStatics::DoesSaveGameExist(*RTSCampaignStorage::PreviousSlot(Base),0));
 Summary.Reset();
 TestFalse(TEXT("Cleaning an empty slot reports nothing deleted"),ARTSCampaign::DeleteSlotRecords(2,Summary));
 TestTrue(*FString::Printf(TEXT("Empty cleanup explains itself: %s"),*Summary),Summary.Contains(TEXT("0 个文件")));
 const bool bProductionAfter=UGameplayStatics::DoesSaveGameExist(Production,0);
 const int64 ProductionSizeAfter=bProductionAfter?IFileManager::Get().FileSize(*RTSCampaignStorage::SlotPath(Production)):-1;
 TestTrue(TEXT("Production save state is unchanged"),bProductionBefore==bProductionAfter && ProductionSizeBefore==ProductionSizeAfter);
 TestFalse(TEXT("Cleanup never creates another slot"),UGameplayStatics::DoesSaveGameExist(*Alt3,0));
 AddInfo(TEXT("SLOT_CLEANUP_OK chain=1 isolation=1 empty=1 production=1"));
 return true;
}
namespace
{
 class FCheckCrowdRestore : public IAutomationLatentCommand
 {
  FAutomationTestBase* T;double Began=FPlatformTime::Seconds(),Phase=0;int32 Cycle=0;bool bMoving=false;
  TStrongObjectPtr<URTSCampaignSave> Saved;TWeakObjectPtr<ARTSUnit> Worker,Solo;FVector Start,SoloStart;
 public:
  explicit FCheckCrowdRestore(FAutomationTestBase* Test):T(Test){}
  bool Update() override
  {
   UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;auto* C=W?ARTSCampaign::Get(W):nullptr;
   if(FPlatformTime::Seconds()-Began>210){T->AddError(TEXT("High-count repeated restore timed out"));return true;}
   if(!C || !C->IsReady())return false;
   if(!Saved.IsValid())
   {
    FRTSCampaignPersistenceTestAccess::Prepare(C);int32 Living=0;
    for(TActorIterator<ARTSUnit> It(W);It;++It)Living+=It->IsAlive();
    // Explicit capacity fixture; no natural recruitment/population claim.
    for(int32 I=0;I<400 && Living<180;++I)
     if(FRTSCampaignPersistenceTestAccess::CrowdFixture(C,RTSCampaignTerrain::Ground(C->HomeLocation()+FVector(3500+(I%20)*180,-6000+(I/20)*180,0))))++Living;
    if(!T->TestTrue(TEXT("Capacity fixture reaches at least180 living units"),Living>=180))return true;
    FRTSOrder Stop;Stop.Type=ERTSOrderType::Stop;
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0)It->ReceiveOrder(Stop);
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData()->BalanceId==TEXT("Swordman"))
    {FRTSOrder Move;Move.Type=ERTSOrderType::Move;Move.TargetLocation=C->HomeLocation()+FVector(2000,-2200,0);It->ReceiveOrder(Move);break;}
    FString Error;Saved.Reset(FRTSCampaignPersistenceTestAccess::Capture(C,Error));
    if(!T->TestTrue(FString(TEXT("High-count snapshot validates: "))+Error,Saved.IsValid() && FRTSCampaignPersistenceTestAccess::Validate(C,Saved.Get())))return true;
   }
   if(!bMoving)
   {
    FString Error;if(!T->TestTrue(FString(TEXT("Repeated high-count restore: "))+Error,FRTSCampaignPersistenceTestAccess::Restore(C,Saved.Get(),Error)))return true;
    auto* Manager=UCrowdManager::GetCurrent(W);int32 Living=0,Valid=0;
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive())
    {
     ++Living;auto* AI=Cast<AAIController>(It->GetController());auto* Crowd=AI?Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent()):nullptr;
     Valid+=Manager && Crowd && Manager->IsAgentValid(Crowd);
    }
    T->AddInfo(FString::Printf(TEXT("RESTORE_CROWD cycle=%d living=%d valid=%d fixture=1"),Cycle+1,Living,Valid));
    if(!T->TestEqual(TEXT("Every committed living unit has a valid Crowd agent"),Valid,Living))return true;
    Solo.Reset();for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetCurrentOrder().Type==ERTSOrderType::Move && !It->GetMoveGroup()){Solo=*It;SoloStart=It->GetActorLocation();break;}
    if(!T->TestTrue(TEXT("Standalone move retained after restore"),Solo.IsValid()))return true;
    ARTSResourceNode* Wood=nullptr;ARTSUnit* Pick=nullptr;double Best=DBL_MAX;
    for(TActorIterator<ARTSResourceNode> N(W);N;++N)if(N->GetResourceType()==ERTSResource::Wood && N->CanGather(0) && N->GetRemaining()>0)
     for(TActorIterator<ARTSUnit> U(W);U;++U)if(U->IsAlive() && U->GetTeam()==0 && U->GetUnitData()->bWorker)
     {const double D=FVector::DistSquared2D(U->GetActorLocation(),N->GetActorLocation());if(D<Best){Best=D;Wood=*N;Pick=*U;}}
    if(!Pick || !Wood){T->AddError(TEXT("Missing post-load worker/wood"));return true;}
    Worker=Pick;Start=Pick->GetActorLocation();Phase=FPlatformTime::Seconds();URTSOrderLibrary::IssueWorkOrder({Pick},Wood);bMoving=true;return false;
   }
   if(!Worker.IsValid()){T->AddError(TEXT("Post-load worker disappeared"));return true;}
   if(Solo.IsValid() && FVector::Dist2D(SoloStart,Solo->GetActorLocation())>=150 && FVector::Dist2D(Start,Worker->GetActorLocation())>=150 && Worker->GetGatherer()->GetCarriedAmount()>0)
   {
    T->AddInfo(FString::Printf(TEXT("RESTORE_CROWD_GATHER cycle=%d moved=%.1f cargo=%d standaloneMoved=%.1f"),Cycle+1,FVector::Dist2D(Start,Worker->GetActorLocation()),Worker->GetGatherer()->GetCarriedAmount(),FVector::Dist2D(SoloStart,Solo->GetActorLocation())));
    bMoving=false;if(++Cycle==3){T->AddInfo(TEXT("RESTORE_CROWD_OK cycles=3 highCount=1 actualGather=1 fixture=1 physicalInput=0"));return true;}return false;
   }
   if(FPlatformTime::Seconds()-Phase>60){T->AddError(FString::Printf(TEXT("Post-load gather stalled cycle=%d moved=%.1f cargo=%d"),Cycle+1,FVector::Dist2D(Start,Worker->GetActorLocation()),Worker->GetGatherer()->GetCarriedAmount()));return true;}
   return false;
  }
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignCrowdRestore,"MiniRTS.Persistence.RepeatedRestoreMovement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignCrowdRestore::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("CrowdRestore"));
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckCrowdRestore(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
namespace
{
 class FCheckPopulationRecruitment : public IAutomationLatentCommand
 {
  FAutomationTestBase* T;double Began=FPlatformTime::Seconds();
 public:
  explicit FCheckPopulationRecruitment(FAutomationTestBase* Test):T(Test){}
  bool Update() override
  {
   auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;auto* C=W?ARTSCampaign::Get(W):nullptr;
   if(FPlatformTime::Seconds()-Began>100){T->AddError(TEXT("Population recruitment watchdog"));return true;}
   if(!C || !C->IsReady())return false;
   auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav || Nav->IsNavigationBuildInProgress())return false;
   FRTSCampaignPersistenceTestAccess::Prepare(C);FRTSCampaignPersistenceTestAccess::PopulationFixture(C);
   auto* Stock=URTSBalanceSubsystem::Get(W)->GetResources(0);Stock->SetAmounts({{ERTSResource::Wood,5000},{ERTSResource::Food,5000}});
   int32 Workers=0;
   for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0){FRTSOrder Stop;Stop.Type=ERTSOrderType::Stop;It->ReceiveOrder(Stop);Workers+=It->GetUnitData()->bWorker;}
   // Explicit population fixture, not natural recruitment/progression evidence.
   for(int32 X=-1800;X<=1800 && Workers<24;X+=180)for(int32 Y=-1800;Y<=1800 && Workers<24;Y+=180)
   {FNavLocation P;if(Nav->ProjectPointToNavigation(C->HomeLocation()+FVector(X,Y,0),P,FVector(20,20,120)))if(FRTSCampaignPersistenceTestAccess::CrowdFixture(C,P.Location))++Workers;}
   if(!T->TestEqual(TEXT("Fixture has 24 real living workers"),Workers,24))return true;
   C->SelectServiceBuilding(TEXT("TownHall"));auto* Hall=C->FindServiceBuilding(TEXT("TownHall"));if(!T->TestNotNull(TEXT("Selected real worker producer"),Hall))return true;
   const int32 Wood=Stock->GetAmount(ERTSResource::Wood),Food=Stock->GetAmount(ERTSResource::Food);const auto Cost=C->GetTrainingCost(TEXT("Peasant"));
   if(!T->TestTrue(TEXT("25th worker is actually paid and queued"),C->Train(TEXT("Peasant"))))return true;
   T->TestTrue(TEXT("26th worker uses last total slot"),C->Train(TEXT("Peasant")));
   T->TestEqual(TEXT("Two actual queue reservations"),Hall->GetProducer()->GetQueue().Num(),2);
   T->TestFalse(TEXT("Queued population blocks a 27th worker at total 32"),C->Train(TEXT("Peasant")));
   T->TestEqual(TEXT("Only two wood bills"),Stock->GetAmount(ERTSResource::Wood),Wood-2*Cost.FindRef(ERTSResource::Wood));
   T->TestEqual(TEXT("Only two food bills"),Stock->GetAmount(ERTSResource::Food),Food-2*Cost.FindRef(ERTSResource::Food));
   T->TestTrue(TEXT("Overview includes queued population"),C->GetOverview().Contains(TEXT("排队2 / 32")));
   auto CheckPopulationHUD=[&](int32 Limit)
   {
    TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Widgets,URTSCampaignWidget::StaticClass(),false);
    auto* HUD=Widgets.Num()?Cast<URTSCampaignWidget>(Widgets[0]):nullptr;
    if(!T->TestNotNull(TEXT("Population HUD exists"),HUD))return;
    HUD->RefreshSelectedObject();
    auto* Text=Cast<UTextBlock>(HUD->WidgetTree->FindWidget(TEXT("CampaignPopulation")));
    if(!T->TestNotNull(TEXT("Permanent population label exists"),Text))return;
    T->TestTrue(TEXT("Population label remains visible"),Text->IsVisible());
    T->TestTrue(TEXT("Population label shows actual maximum"),Text->GetText().ToString().Contains(FString::Printf(TEXT("最大人口 %d"),Limit)));
    T->AddInfo(TEXT("POPULATION_HUD ")+Text->GetText().ToString());
   };
   CheckPopulationHUD(32);
   T->TestTrue(TEXT("Cancel refunds one reservation"),Hall->GetProducer()->CancelLast());T->TestTrue(TEXT("Released slot permits replacement"),C->Train(TEXT("Peasant")));
   FString Error;auto* S=FRTSCampaignPersistenceTestAccess::Capture(C,Error);if(!T->TestNotNull(TEXT("Current population snapshot"),S))return true;
   auto* EnemyQueued=DuplicateObject<URTSCampaignSave>(S,GetTransientPackage());int32 EnemyBarracks=INDEX_NONE;
   for(int32 I=0;I<EnemyQueued->BuildingKinds.Num();++I)if(EnemyQueued->BuildingTeams[I]==1 && EnemyQueued->BuildingKinds[I]==TEXT("Barracks")){EnemyBarracks=I;break;}
   if(!T->TestTrue(TEXT("Enemy queue validation fixture has a real faction barracks"),EnemyBarracks>=0))return true;
   const int32 F=EnemyQueued->BuildingFactions[EnemyBarracks];FRTSPaidTrainingSnapshot Advanced;Advanced.Kind=TEXT("Spearman");Advanced.Cost=C->GetTrainingCost(Advanced.Kind);Advanced.TrainSeconds=4;
   auto& Production=EnemyQueued->BuildingWork[EnemyBarracks].Production;Production=FRTSProductionSnapshot();Production.Orders.Add(Advanced);EnemyQueued->FactionEconomies[F].Levels.Add(Advanced.Kind,1);
   T->TestTrue(TEXT("Enemy paid research validates its own advanced queue without player research"),FRTSCampaignPersistenceTestAccess::Validate(C,EnemyQueued));
   EnemyQueued->FactionEconomies[F].Levels.Remove(Advanced.Kind);T->TestFalse(TEXT("Unresearched enemy advanced queue stays rejected"),FRTSCampaignPersistenceTestAccess::Validate(C,EnemyQueued));
   S->FacilityMask|=1;S->PopulationPurchases=7;S->LegacyPopulationBonus=32;
   T->TestTrue(TEXT("Legacy maximum valid in schema15"),FRTSCampaignPersistenceTestAccess::Validate(C,S));
   S->PopulationPurchases=8;T->TestFalse(TEXT("Legacy capacity overflow rejected"),FRTSCampaignPersistenceTestAccess::Validate(C,S));
   S->PopulationPurchases=0;S->LegacyPopulationBonus=8;T->TestFalse(TEXT("Invented legacy bonus rejected"),FRTSCampaignPersistenceTestAccess::Validate(C,S));
   S->LegacyPopulationBonus=0;S->PopulationPurchases=11;T->TestTrue(TEXT("New maximum validates"),FRTSCampaignPersistenceTestAccess::Validate(C,S));
   S->Version=13;S->PopulationPurchases=0;
   if(!T->TestTrue(FString(TEXT("Restore legacy13 housing: "))+Error,FRTSCampaignPersistenceTestAccess::Restore(C,S,Error)))return true;
   T->TestEqual(TEXT("Actual legacy13 restoration retains 64"),C->GetPopulationLimit(),64);
   CheckPopulationHUD(64);
   S->Version=14;S->PopulationPurchases=2;
   if(!T->TestTrue(FString(TEXT("Restore legacy14 housing: "))+Error,FRTSCampaignPersistenceTestAccess::Restore(C,S,Error)))return true;
   T->TestEqual(TEXT("Actual legacy14 restoration retains 80"),C->GetPopulationLimit(),80);
   CheckPopulationHUD(80);
   auto* Migrated=FRTSCampaignPersistenceTestAccess::Capture(C,Error);if(!T->TestNotNull(TEXT("Capture migrated schema15"),Migrated))return true;
   T->TestEqual(TEXT("Compatibility bonus persists"),Migrated->LegacyPopulationBonus,32);
   TArray<uint8> Bytes;T->TestTrue(TEXT("Migrated snapshot encodes"),RTSCampaignStorage::Encode(Migrated,Bytes));auto* Copy=Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Bytes));
   if(!Copy || !T->TestTrue(TEXT("Restore schema15 memory roundtrip"),FRTSCampaignPersistenceTestAccess::Restore(C,Copy,Error)))return true;
   T->TestEqual(TEXT("Schema15 roundtrip still retains 80"),C->GetPopulationLimit(),80);
   T->AddInfo(TEXT("POPULATION_RECRUITMENT_OK realWorkers=24 paid25th=1 queuedTotal=32 rejectedNoCharge=1 cancelReleasesSlot=1 schema15Bounds=1 legacy13=64 legacy14=80 roundtrip15=80 fixture=1 physicalInput=0"));return true;
  }
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPopulationRecruitment,"MiniRTS.Persistence.PopulationRecruitmentAndCompatibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSPopulationRecruitment::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("Population"));
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckPopulationRecruitment(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
namespace
{
 class FCheckEnemyNaturalHaul : public IAutomationLatentCommand
 {
  FAutomationTestBase* T;double Began=FPlatformTime::Seconds();bool Setup=false;int32 BeforeWood[4]={},BeforeFood[4]={};TWeakObjectPtr<ARTSUnit> Workers[4][2];TWeakObjectPtr<ARTSResourceNode> Nodes[4][2];FVector Starts[4][2];
 public:
  explicit FCheckEnemyNaturalHaul(FAutomationTestBase* Test):T(Test){}
  bool Update() override
  {
   auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;auto* C=W?ARTSCampaign::Get(W):nullptr;
   // Wall-clock bound: under a loaded editor the same work takes far longer than the 25s it
   // needs when idle, so this only catches a real hang. The reported completionSeconds keeps
   // any genuine slowdown visible in the evidence.
   if(FPlatformTime::Seconds()-Began>600)
   {
    FString D;for(int32 F=0;F<4;++F)
    {
     auto* U=Workers[F][0].Get();auto* A=(C && U)?C->GetActorAccount(U):nullptr;auto* G=U?U->GetGatherer():nullptr;
     D+=FString::Printf(TEXT("[F%d wood=%d/%d food=%d/%d cargo=%d status=%s node=%d remain=%d]"),F,A?A->GetAmount(ERTSResource::Wood):-1,BeforeWood[F],
      A?A->GetAmount(ERTSResource::Food):-1,BeforeFood[F],G?G->GetCarriedAmount():-1,G?*G->GetStatus():TEXT("none"),
      Nodes[F][0].IsValid()?1:0,Nodes[F][0].IsValid()?Nodes[F][0]->GetRemaining():-1);
    }
    T->AddError(FString::Printf(TEXT("Enemy natural haul watchdog setup=%d %s"),Setup?1:0,*D));return true;
   }
   if(!C || !C->IsReady())return false;
   auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav || Nav->IsNavigationBuildInProgress())return false;
   if(!Setup)
   {
    // Freeze AI spending only, so account increases prove real haul/deposit.
    C->SetActorTickEnabled(false);for(TActorIterator<ARTSCampaignEconomyController> It(W);It;++It)It->Destroy();
    for(TActorIterator<ARTSBuilding> It(W);It;++It)if(C->GetActorFaction(*It)>=0)It->GetProducer()->SetComponentTickEnabled(false);
    FRTSCampaignPersistenceTestAccess::Prepare(C);
    FString Error;auto* S=FRTSCampaignPersistenceTestAccess::Capture(C,Error);if(!T->TestNotNull(TEXT("Fresh enemy layout snapshot"),S))return true;
    T->TestEqual(TEXT("All four factions have all eight finite resources"),S->EnemyNodes.Num(),32);
    int32 Counts[4]={};for(const auto& N:S->EnemyNodes){if(N.Faction>=0 && N.Faction<4)++Counts[N.Faction];T->TestEqual(TEXT("Every node retains finite initial stock"),N.Remaining,6000);}
    for(int32 F=0;F<4;++F)
    {
     T->TestEqual(TEXT("Every faction has eight types"),Counts[F],8);int32 I=0;
     for(TActorIterator<ARTSUnit> It(W);It && I<2;++It)if(It->IsAlive() && It->GetUnitData()->bWorker && C->GetActorFaction(*It)==F){Workers[F][I]=*It;Starts[F][I]=It->GetActorLocation();++I;}
     if(!T->TestEqual(TEXT("Two original workers per faction"),I,2))return true;
     auto* Account=C->GetActorAccount(Workers[F][0].Get());BeforeWood[F]=Account->GetAmount(ERTSResource::Wood);BeforeFood[F]=Account->GetAmount(ERTSResource::Food);
     T->TestEqual(TEXT("No extra bootstrap wood"),BeforeWood[F],240);T->TestEqual(TEXT("No extra bootstrap food"),BeforeFood[F],300);
     for(int32 J=0;J<2;++J)
     {
      auto* N=FRTSCampaignPersistenceTestAccess::EnemyNode(C,F,J==0?ERTSResource::Wood:ERTSResource::Food);if(!T->TestNotNull(TEXT("Natural staple node"),N))return true;Nodes[F][J]=N;
      T->TestEqual(TEXT("Four real work slots retained"),N->GetGatherSlots(),4);T->TestEqual(TEXT("No passive recovery"),N->GetRecoveryPerSecond(),0.f);
      URTSOrderLibrary::IssueWorkOrder({Workers[F][J].Get()},N);
     }
    }
    Setup=true;return false;
   }
   for(int32 F=0;F<4;++F)
   {
    auto* A=C->GetActorAccount(Workers[F][0].Get());if(!A || A->GetAmount(ERTSResource::Wood)<=BeforeWood[F] || A->GetAmount(ERTSResource::Food)<=BeforeFood[F])return false;
   }
   for(int32 F=0;F<4;++F)for(int32 J=0;J<2;++J){T->TestTrue(TEXT("Actual worker travelled"),FVector::Dist2D(Starts[F][J],Workers[F][J]->GetActorLocation())>100);T->TestTrue(TEXT("Finite node was physically harvested"),Nodes[F][J]->GetRemaining()<6000);}
   T->AddInfo(FString::Printf(TEXT("ENEMY_NATURAL_HAUL_OK factions=4 resources=32 stapleRoutes=8 actualDeposit=1 stockInjection=0 freeWorkers=0 frozenAISpending=1 completionSeconds=%.1f physicalInput=0"),FPlatformTime::Seconds()-Began));return true;
  }
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnemyNaturalHaul,"MiniRTS.EnemyEconomy.NaturalTerritoryHaul",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSEnemyNaturalHaul::RunTest(const FString&)
{
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("EnemyHaul"));
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FCheckEnemyNaturalHaul(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
#endif
