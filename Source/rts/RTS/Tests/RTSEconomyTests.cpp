#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/RTSEconomyScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "HAL/IConsoleManager.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSGatheringTest, "MiniRTS.Economy.GatherDeliveryAndInterrupt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSConstructionTest, "MiniRTS.Economy.ConstructionAndProduction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSQueueTest, "MiniRTS.Economy.BlockedQueueAndRefund", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFarmTowerTest, "MiniRTS.Economy.FarmAndTower", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	class FCheckEconomy : public IAutomationLatentCommand
	{
	public:
		FCheckEconomy(FAutomationTestBase* InTest, int32 InMode) : Test(InTest), Mode(InMode) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
			PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
			if (Stage < 0)
			{
				IConsoleManager::Get().ProcessUserConsoleInput(TEXT("rts.TestEconomy"), *GLog, World);
				for (TActorIterator<ARTSUnit> It(World); It; ++It) { Scenario.Workers.Add(*It); }
				for (TActorIterator<ARTSBuilding> It(World); It; ++It) { if (It->GetBuildingId() == TEXT("TownHall")) { Scenario.Hall = *It; } }
				for (TActorIterator<ARTSResourceNode> It(World); It; ++It) { if (It->GetResourceType() == ERTSResource::Wood) { Scenario.Wood = *It; } else { Scenario.Food = *It; } }
				if (!Balance || !PC || Scenario.Workers.Num() != 3 || !Scenario.Hall || !Scenario.Wood || !Scenario.Food)
				{ Test->AddError(TEXT("Economy command failed to create scenario")); return true; }
				Stock = Balance->GetResources(0); Worker = Scenario.Workers[0];
				Test->TestEqual(TEXT("Three workers selected"), PC->GetSelection()->GetSelectedUnits().Num(), 3);
				const FRTSBalanceRow* Row = Balance->Find(TEXT("Peasant"));
				Test->TestTrue(TEXT("CSV Peasant is a worker"), Row && Row->bWorker && Worker->GetUnitData()->bWorker);
				Test->TestEqual(TEXT("CSV health applied"), Worker->GetHealth()->GetMaxHealth(), Row->MaxHealth);
				Test->TestEqual(TEXT("Worker regeneration is deliberately slow"), Worker->GetHealth()->GetRegenerationRate(), 0.1f);
				Test->TestEqual(TEXT("Worker regeneration waits after damage"), Worker->GetHealth()->GetRegenerationDelay(), 30.0f);
				Test->TestEqual(TEXT("CSV capacity applied"), Worker->GetUnitData()->CarryCapacity, Row->CarryCapacity);
				Next(World);
			}
			if (World->GetTimeSeconds() - StageAt > 50)
			{
				Test->AddError(FString::Printf(TEXT("Economy mode %d stage %d timed out; worker=%s pos=%s cargo=%d W=%d F=%d"), Mode, Stage,
					*Worker->GetGatherer()->GetStatus(), *Worker->GetActorLocation().ToCompactString(), Worker->GetGatherer()->GetCarriedAmount(), Stock->GetAmount(ERTSResource::Wood), Stock->GetAmount(ERTSResource::Food)));
				return true;
			}
			if (Stage == 0 && !Elapsed(World, 1)) { return false; }
			return Mode == 0 ? Gather(World) : Mode == 1 ? Construct(World, Balance) : Mode == 2 ? Queue(World, Balance) : FarmTower(World, Balance);
		}
	private:
		void Next(UWorld* World) { ++Stage; StageAt = World->GetTimeSeconds(); }
		bool Elapsed(UWorld* World, float Seconds) { return World->GetTimeSeconds() - StageAt >= Seconds; }
		bool FindPlace(URTSBalanceSubsystem* Balance, FName Id, FVector& Out)
		{
			FString Error; double Best = TNumericLimits<double>::Max();
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(PC->GetWorld());
			for (int32 X = -1800; X <= 1800; X += 100)
			{
				for (int32 Y = -1800; Y <= 1800; Y += 100)
				{
					FNavLocation P;
					if (!Nav->ProjectPointToNavigation(FVector(X, Y, 10), P, FVector(15, 15, 30)) || !Balance->CanPlace(Id, P.Location, Error)) { continue; }
					const double D = FVector::DistSquared2D(Worker->GetActorLocation(), P.Location);
					if (D < Best && D > FMath::Square(250.0)) { Out = P.Location; Best = D; }
				}
			}
			return Best < TNumericLimits<double>::Max();
		}
		bool Gather(UWorld* World)
		{
			if (Stage == 0)
			{
				// Leave exactly one load plus a partial load to verify final delivery at depletion.
				Scenario.Wood->Extract(487);
				URTSOrderLibrary::IssueWorkOrder({Worker}, Scenario.Wood);
				Next(World);
			}
			else if (Stage == 1)
			{
				const int32 Cargo = Worker->GetGatherer()->GetCarriedAmount();
				if (Cargo == 0) { return false; }
				Test->TestEqual(TEXT("Cargo not credited before delivery"), Stock->GetAmount(ERTSResource::Wood), 400);
				Test->TestEqual(TEXT("Node and cargo conserve wood"), Scenario.Wood->GetRemaining() + Cargo, 13);
				URTSOrderLibrary::IssueStopOrder({Worker}); Remaining = Scenario.Wood->GetRemaining(); SavedCargo = Cargo;
				Next(World);
			}
			else if (Stage == 2 && Elapsed(World, 1))
			{
				Test->TestEqual(TEXT("Stop halts extraction"), Scenario.Wood->GetRemaining(), Remaining);
				Test->TestEqual(TEXT("Stop preserves cargo"), Worker->GetGatherer()->GetCarriedAmount(), SavedCargo);
				URTSOrderLibrary::IssueWorkOrder({Worker}, Scenario.Wood); Next(World);
			}
			else if (Stage == 3 && Stock->GetAmount(ERTSResource::Wood) == 413)
			{
				Test->TestEqual(TEXT("Depleted node fully delivered"), Scenario.Wood->GetRemaining(), 0);
				Test->TestEqual(TEXT("No cargo duplicated"), Worker->GetGatherer()->GetCarriedAmount(), 0);
				Scenario.Food->Extract(493);
				URTSOrderLibrary::IssueWorkOrder(Scenario.Workers, Scenario.Food); Next(World);
			}
			else if (Stage == 4 && Stock->GetAmount(ERTSResource::Food) == 307)
			{
				Test->TestEqual(TEXT("Food depleted and delivered"), Scenario.Food->GetRemaining(), 0);
				Test->AddInfo(TEXT("Actual worker trips delivered exactly 13 wood and 7 food; stop/resume conserved cargo."));
				return true;
			}
			return false;
		}
		bool Construct(UWorld* World, URTSBalanceSubsystem* Balance)
		{
			FString Error;
			if (Stage == 0)
			{
				PC->BeginBuildingPlacement(TEXT("Barracks")); PC->CancelBuildingPlacement();
				Test->TestEqual(TEXT("Cancel preview costs nothing"), Stock->GetAmount(ERTSResource::Wood), 400);
				Test->TestNull(TEXT("Overlap rejected"), Balance->PlaceBuilding(TEXT("Barracks"), 0, Scenario.Hall->GetActorLocation(), Error));
				Test->TestEqual(TEXT("Invalid placement costs nothing"), Stock->GetAmount(ERTSResource::Wood), 400);
				if (!FindPlace(Balance, TEXT("Barracks"), Place)) { Test->AddError(TEXT("No barracks placement found")); return true; }
				PC->GetSelection()->SelectSingle(Worker, false);
				PC->BeginBuildingPlacement(TEXT("Barracks"));
				if (!PC->ConfirmBuildingPlacement(Place)) { Test->AddError(PC->GetEconomyMessage()); return true; }
				for (TActorIterator<ARTSBuilding> It(World); It; ++It) { if (It->GetBuildingId() == TEXT("Barracks")) { Site = *It; } }
				if (!Site) { Test->AddError(TEXT("No site after confirmation")); return true; }
				Test->TestEqual(TEXT("Confirmation spends CSV building cost once"), Stock->GetAmount(ERTSResource::Wood), 400 - Balance->Find(TEXT("Barracks"))->WoodCost);
				Test->TestFalse(TEXT("Cannot train during construction"), Site->GetProducer()->Enqueue(TEXT("Swordman"), Error));
				URTSOrderLibrary::IssueStopOrder({Worker}); Next(World);
			}
			else if (Stage == 1 && Elapsed(World, 1))
			{
				Test->TestEqual(TEXT("No construction without worker"), Site->GetBuildProgress(), 0.0f);
				URTSOrderLibrary::IssueWorkOrder({Worker}, Site); Next(World);
			}
			else if (Stage == 2 && Site->GetBuildProgress() >= 0.25f)
			{
				Test->TestEqual(TEXT("Working site status"), Site->GetConstructionStatus(), FString(TEXT("正在施工")));
				URTSOrderLibrary::IssueStopOrder({Worker});
				SavedBuildProgress = Site->GetBuildProgress();
				SavedBuildWood = Stock->GetAmount(ERTSResource::Wood);
				SavedBuildFood = Stock->GetAmount(ERTSResource::Food);
				Next(World);
			}
			else if (Stage == 3 && Elapsed(World, 1))
			{
				Test->TestEqual(TEXT("Interrupted construction preserves progress"), Site->GetBuildProgress(), SavedBuildProgress);
				Test->TestTrue(TEXT("Abandoned site explains resume"), Site->GetConstructionStatus().Contains(TEXT("右键续建")));
				Worker->GetHealth()->ApplyDamage(100000, nullptr);
				Next(World);
			}
			else if (Stage == 4 && Elapsed(World, 1))
			{
				Test->TestEqual(TEXT("Worker death preserves progress"), Site->GetBuildProgress(), SavedBuildProgress);
				FVector ReplacementFeet;
				if (!Balance->FindSpawnPoint(Scenario.Hall->GetActorLocation(), 400, ReplacementFeet)) { Test->AddError(TEXT("No replacement worker location")); return true; }
				Worker = Balance->SpawnUnit(TEXT("Peasant"), 0, ReplacementFeet);
				if (!Worker) { Test->AddError(TEXT("Replacement worker spawn failed")); return true; }
				URTSOrderLibrary::IssueWorkOrder({Worker}, Site); Next(World);
			}
			else if (Stage == 5 && Site->IsComplete())
			{
				Test->TestTrue(TEXT("Worker physically completed construction"), FVector::Dist2D(Worker->GetActorLocation(), Site->GetActorLocation()) < Site->GetFootprint() + 100);
				Test->TestEqual(TEXT("Resuming does not charge wood twice"), Stock->GetAmount(ERTSResource::Wood), SavedBuildWood);
				Test->TestEqual(TEXT("Resuming does not charge food twice"), Stock->GetAmount(ERTSResource::Food), SavedBuildFood);
				const int32 W = Stock->GetAmount(ERTSResource::Wood), F = Stock->GetAmount(ERTSResource::Food);
				Test->TestTrue(TEXT("Swordman enqueued"), Site->GetProducer()->Enqueue(TEXT("Swordman"), Error));
				Test->TestTrue(TEXT("Archer enqueued"), Site->GetProducer()->Enqueue(TEXT("Archer"), Error));
				Test->TestEqual(TEXT("Train cost from CSV"), Stock->GetAmount(ERTSResource::Wood), W - Balance->Find(TEXT("Swordman"))->WoodCost - Balance->Find(TEXT("Archer"))->WoodCost);
				Test->TestTrue(TEXT("Cancel last queued unit"), Site->GetProducer()->CancelLast());
				Test->TestEqual(TEXT("Cancel refunds original food cost"), Stock->GetAmount(ERTSResource::Food), F - Balance->Find(TEXT("Swordman"))->FoodCost);
				PC->SelectBuilding(Site); Next(World);
			}
			else if (Stage == 6 && Site->GetProducer()->GetQueue().IsEmpty())
			{
				ARTSUnit* Trained = nullptr;
				for (TActorIterator<ARTSUnit> It(World); It; ++It) { if (It->GetUnitData()->BalanceId == TEXT("Swordman")) { Trained = *It; } }
				Test->TestNotNull(TEXT("Actual trained unit spawned"), Trained);
				if (Trained) { Test->TestEqual(TEXT("CSV trained stats"), Trained->GetHealth()->GetMaxHealth(), Balance->Find(TEXT("Swordman"))->MaxHealth); }
				Test->TestEqual(TEXT("Correct building selected"), PC->GetSelectedBuilding(), Site);
				Test->AddInfo(TEXT("Placement, worker construction, FIFO training, cancellation and CSV costs/stats passed."));
				return true;
			}
			return false;
		}
		bool Queue(UWorld* World, URTSBalanceSubsystem* Balance)
		{
			FString Error;
			if (Stage == 0)
			{
				FString CSV;
				if (!FFileHelper::LoadFileToString(CSV, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RTS/Data/Balance/RTSBalance.csv"))))
				{ Test->AddError(TEXT("Cannot read balance CSV for validation test")); return true; }
				// Keep both durations valid so only the ID/kind mismatch can reject a row.
				TArray<FString> Lines; CSV.ParseIntoArrayLines(Lines);
				for (int32 i = 1; i < Lines.Num(); ++i)
				{
					TArray<FString> Cells; Lines[i].ParseIntoArray(Cells, TEXT(","), false);
					if (Cells.Num() != 19) { Test->AddError(TEXT("Unexpected balance CSV columns")); return true; }
					Cells[15] = TEXT("8"); Cells[16] = TEXT("4");
					Lines[i] = FString::Join(Cells, TEXT(","));
				}
				const FString KindFixture = FString::Join(Lines, TEXT("\n"));
				Test->TestTrue(TEXT("Kind fixture otherwise valid"), Balance->ReloadFromCSV(KindFixture, Error));
				const FRTSBalanceRow* Original = Balance->Find(TEXT("Swordman"));
				for (const FString& Id : {FString("Peasant"), FString("Swordman"), FString("Archer"), FString("TownHall"), FString("Barracks"), FString("Farm"), FString("Tower")})
				{
					const bool bUnit = Id == TEXT("Peasant") || Id == TEXT("Swordman") || Id == TEXT("Archer");
					FString InvalidCSV = KindFixture;
					// Anchor on the row start: tier names such as HeavySwordman share the lower-tier prefix.
					const FString From = FString(TEXT("\n")) + Id + (bUnit ? TEXT(",Unit,") : TEXT(",Building,"));
					const FString To = FString(TEXT("\n")) + Id + (bUnit ? TEXT(",Building,") : TEXT(",Unit,"));
					Test->TestEqual(TEXT("Validation fixture replaces one row kind"), InvalidCSV.ReplaceInline(*From, *To), 1);
					Test->TestFalse(*FString::Printf(TEXT("Wrong kind rejected for %s"), *Id), Balance->ReloadFromCSV(InvalidCSV, Error));
					Test->TestEqual(TEXT("Rejected reload preserves previous table"), Balance->Find(TEXT("Swordman")), Original);
				}
				Test->TestTrue(TEXT("Valid CSV reload succeeds"), Balance->ReloadFromCSV(CSV, Error));
				Test->TestFalse(TEXT("TownHall rejects military unit"), Scenario.Hall->GetProducer()->Enqueue(TEXT("Swordman"), Error));
				Stock->Add(ERTSResource::Food, -Stock->GetAmount(ERTSResource::Food));
				Test->TestFalse(TEXT("Insufficient resources reject enqueue"), Scenario.Hall->GetProducer()->Enqueue(TEXT("Peasant"), Error));
				Stock->Add(ERTSResource::Food, 1000);
				for (int32 i = 0; i < 5; ++i) { Test->TestTrue(TEXT("Enqueue within capacity"), Scenario.Hall->GetProducer()->Enqueue(TEXT("Peasant"), Error)); }
				const int32 Paid = Stock->GetAmount(ERTSResource::Food);
				Test->TestFalse(TEXT("Sixth order rejected"), Scenario.Hall->GetProducer()->Enqueue(TEXT("Peasant"), Error));
				Test->TestEqual(TEXT("Full queue does not spend"), Stock->GetAmount(ERTSResource::Food), Paid);
				for (int32 i = 0; i < 4; ++i) { Scenario.Hall->GetProducer()->CancelLast(); }
				Block = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Block); Block->SetRootComponent(Box);
				Box->SetCanEverAffectNavigation(false); Box->SetBoxExtent(FVector(850, 850, 130));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
				Block->SetActorLocation(Scenario.Hall->GetActorLocation() + FVector(0, 0, 130));
				SavedFood = Stock->GetAmount(ERTSResource::Food); Next(World);
			}
			else if (Stage == 1 && Elapsed(World, 5))
			{
				Test->TestTrue(TEXT("Blocked exit keeps complete order"), Scenario.Hall->GetProducer()->IsSpawnBlocked() && Scenario.Hall->GetProducer()->GetQueue().Num() == 1 && Scenario.Hall->GetProducer()->GetProgress() == 1);
				Test->TestEqual(TEXT("Blocked retry does not spend"), Stock->GetAmount(ERTSResource::Food), SavedFood);
				Test->TestTrue(TEXT("Blocked order can be cancelled"), Scenario.Hall->GetProducer()->CancelLast());
				Test->TestEqual(TEXT("Blocked refund exact"), Stock->GetAmount(ERTSResource::Food), 1000);
				Scenario.Hall->GetProducer()->Enqueue(TEXT("Peasant"), Error); Block->Destroy(); Next(World);
			}
			else if (Stage == 2 && Elapsed(World, 4) && Scenario.Hall->GetProducer()->GetQueue().IsEmpty())
			{
				int32 Count = 0; for (TActorIterator<ARTSUnit> It(World); It; ++It) { ++Count; }
				Test->TestEqual(TEXT("Unblocked production spawns exactly one unit"), Count, 4);
				Test->AddInfo(TEXT("Queue capacity, insufficient funds, blocked retention/retry and full cancellation refund passed.")); return true;
			}
			return false;
		}
		bool FarmTower(UWorld* World, URTSBalanceSubsystem* Balance)
		{
			FString Error;
			if (Stage == 0)
			{
				if (!FindPlace(Balance, TEXT("Farm"), Place)) { Test->AddError(TEXT("No farm location")); return true; }
				Site = Balance->PlaceBuilding(TEXT("Farm"), 0, Place, Error);
				if (!Site) { Test->AddError(Error); return true; }
				Site->AddConstructionWork(Balance->Find(TEXT("Farm"))->BuildSeconds);
				Next(World);
			}
			else if (Stage == 1 && Site->GetFoodSource())
			{
				Test->TestTrue(TEXT("Farm food belongs to team"), Site->GetFoodSource()->CanGather(0) && !Site->GetFoodSource()->CanGather(1));
				Test->TestEqual(TEXT("Farm supplies food"), Site->GetFoodSource()->GetResourceType(), ERTSResource::Food);
				Test->TestTrue(TEXT("Farm is renewable"), Site->GetFoodSource()->IsRenewable());
				Test->TestEqual(TEXT("Farm capacity"), Site->GetFoodSource()->GetCapacity(), 300);
				Test->TestEqual(TEXT("Farm slow recovery rate"), Site->GetFoodSource()->GetRecoveryPerSecond(), 0.5f);
				Site->GetFoodSource()->Extract(1); Next(World);
			}
			else if (Stage == 2 && Elapsed(World, 4.5f))
			{
				Test->TestEqual(TEXT("Farm recovery capped at capacity"), Site->GetFoodSource()->GetRemaining(), 300);
				Test->TestEqual(TEXT("First full harvest"), Site->GetFoodSource()->Extract(300), 300);
				Test->TestEqual(TEXT("Farm is exhausted by harvesting"), Site->GetFoodSource()->Extract(300), 0);
				Test->TestTrue(TEXT("Empty farm stays selectable"), !Site->GetFoodSource()->IsHidden() && Site->GetFoodSource()->GetActorEnableCollision());
				URTSOrderLibrary::IssueWorkOrder({Worker}, Site); Next(World);
			}
			else if (Stage == 3 && Elapsed(World, 8) && Stock->GetAmount(ERTSResource::Food) > 300)
			{
				Test->TestTrue(TEXT("Worker keeps gather order while farm recovers"), Worker->GetCurrentOrder().Type == ERTSOrderType::Gather);
				Test->TestTrue(TEXT("Farm recovered and worker harvested without new order"), Worker->GetGatherer()->GetCarriedAmount() > 0 || Stock->GetAmount(ERTSResource::Food) > 300);
				Test->TestTrue(TEXT("Recovery is slow and finite"), Site->GetFoodSource()->GetRemaining() < 10);
				URTSOrderLibrary::IssueStopOrder({Worker});
				if (!FindPlace(Balance, TEXT("Tower"), Place)) { Test->AddError(TEXT("No tower location")); return true; }
				Site = Balance->PlaceBuilding(TEXT("Tower"), 0, Place, Error);
				if (!Site) { Test->AddError(Error); return true; }
				Site->AddConstructionWork(Balance->Find(TEXT("Tower"))->BuildSeconds);
				FVector Feet;
				if (!Balance->FindSpawnPoint(Place, 400, Feet)) { Test->AddError(TEXT("No enemy spawn near tower")); return true; }
				Enemy = Balance->SpawnUnit(TEXT("Peasant"), 1, Feet);
				if (!Enemy) { Test->AddError(TEXT("Enemy spawn failed")); return true; }
				Next(World);
			}
			else if (Stage == 4 && Enemy->GetHealth()->GetHealth() < Enemy->GetHealth()->GetMaxHealth())
			{
				Test->TestTrue(TEXT("Tower enemy in sight"), Site->FindComponentByClass<URTSAttackComponent>()->HasLineOfSight(Enemy));
				Test->AddInfo(TEXT("Farm generated team-owned food, worker delivered it; completed tower shot and damaged an enemy.")); return true;
			}
			return false;
		}
		FAutomationTestBase* Test; int32 Mode; int32 Stage = -1;
		double Started = FPlatformTime::Seconds(), StageAt = 0;
		FRTSEconomyScenario Scenario;
		ARTSHumanController* PC = nullptr;
		URTSResourceComponent* Stock = nullptr;
		ARTSUnit* Worker = nullptr;
		ARTSBuilding* Site = nullptr;
		AActor* Block = nullptr;
		ARTSUnit* Enemy = nullptr;
		FVector Place;
		int32 Remaining = 0, SavedCargo = 0, SavedFood = 0;
		float SavedBuildProgress = 0;
		int32 SavedBuildWood = 0, SavedBuildFood = 0;
	};
	void QueueEconomyTest(FAutomationTestBase* Test, int32 Mode)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckEconomy(Test, Mode));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}
bool FRTSGatheringTest::RunTest(const FString&) { QueueEconomyTest(this, 0); return true; }
bool FRTSConstructionTest::RunTest(const FString&) { QueueEconomyTest(this, 1); return true; }
bool FRTSQueueTest::RunTest(const FString&) { QueueEconomyTest(this, 2); return true; }
bool FRTSFarmTowerTest::RunTest(const FString&) { QueueEconomyTest(this, 3); return true; }
#endif
