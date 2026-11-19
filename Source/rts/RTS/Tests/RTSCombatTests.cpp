#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Tests/RTSCombatScenario.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSAttackComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSProjectile.h"
#include "Data/RTSUnitData.h"
#include "Orders/RTSOrderLibrary.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCombatRulesTest, "MiniRTS.Combat.RulesAndOrders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProjectileTest, "MiniRTS.Combat.ProjectilesAndOcclusion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBattleTest, "MiniRTS.Combat.AutomaticBattle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAcquisitionTest, "MiniRTS.Combat.AcquisitionAndLeash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	class FCheckCombat : public IAutomationLatentCommand
	{
	public:
		FCheckCombat(FAutomationTestBase* InTest, int32 InMode) : Test(InTest), Mode(InMode) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{
				if (FPlatformTime::Seconds() - WaitStarted < 30) { return false; }
				Test->AddError(TEXT("PIE world unavailable.")); return true;
			}
			if (Stage < 0)
			{
				if (Mode == 2)
				{
					IConsoleManager::Get().ProcessUserConsoleInput(TEXT("rts.TestCombat"), *GLog, World);
					for (TActorIterator<ARTSUnit> It(World); It; ++It) { Units.Add(*It); }
					if (Units.Num() != 10) { Test->AddError(TEXT("Combat command did not create ten units.")); return true; }
					ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
					Test->TestTrue(TEXT("Scenario selects five allies"), PC && PC->GetSelection()->GetSelectedUnits().Num() == 5);
				}
				else if (!Setup(World)) { return true; }
				NextStage(World);
			}
			if (Mode == 2) { return CheckBattle(World); }
			if (!A.IsValid() || !B.IsValid() || !Friend.IsValid()) { Test->AddError(TEXT("Fixture disappeared.")); return true; }
			return Mode == 0 ? CheckRules(World) : Mode == 1 ? CheckProjectile(World) : CheckAcquisition(World);
		}
	private:
		void NextStage(UWorld* World) { ++Stage; StageTime = World->GetTimeSeconds(); }
		bool Elapsed(UWorld* World, double Seconds) const { return World->GetTimeSeconds() - StageTime >= Seconds; }
		bool Setup(UWorld* World)
		{
			TArray<ARTSUnit*> Spawned;
			FString Error;
			if (!RTSCombatScenario::Create(World, 1, 0, Spawned, Error)) { Test->AddError(Error); return false; }
			URTSUnitData* Data = DuplicateObject<URTSUnitData>(Spawned[0]->GetUnitData(), World);
			Data->bAutoAcquireTargets = false;
			Data->AttackHitDelay = 0.25f; Data->AttackInterval = 0.4f; Data->AttackDamage = 10;
			Data->bRangedAttack = Mode == 1; Data->AttackRange = Mode == 1 ? 650 : 80;
			Data->ProjectileSpeed = 300;
			const FVector Origin = Spawned[0]->GetActorLocation();
			for (ARTSUnit* Unit : Spawned) { Unit->Destroy(); }
			auto Spawn = [&](FVector Location, uint8 Team)
			{
				const FTransform Transform(Location);
				ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Unit) { Unit->Configure(DuplicateObject<URTSUnitData>(Data, Unit), Team); Unit->FinishSpawning(Transform); }
				return Unit;
			};
			A = Spawn(Origin, 0);
			const FVector Facing = (Spawned[1]->GetActorLocation() - Origin).GetSafeNormal2D();
			B = Spawn(Origin + Facing * (Mode == 1 ? 600 : 120), 1);
			Friend = Spawn(Origin + FVector(250, 0, 0), 0);
			return A.IsValid() && B.IsValid() && Friend.IsValid();
		}
		bool CheckRules(UWorld* World)
		{
			if (Stage == 0)
			{
				Test->TestFalse(TEXT("No friendly fire"), A->GetAttack()->TryStartAttack(Friend.Get()));
				Friend->SetGenericTeamId(FGenericTeamId(RTSTeam::Neutral));
				Test->TestFalse(TEXT("Neutral not hostile"), URTSAttackComponent::IsHostileTarget(A.Get(), Friend.Get()));
				Friend->SetGenericTeamId(FGenericTeamId(0));
				Test->TestTrue(TEXT("Enemy swing starts"), A->GetAttack()->TryStartAttack(B.Get()));
				Test->TestEqual(TEXT("Damage delayed until impact"), B->GetHealth()->GetHealth(), 120.0f);
				URTSOrderLibrary::IssueStopOrder({A.Get()});
				NextStage(World);
			}
			else if (Stage == 1 && Elapsed(World, 0.5))
			{
				Test->TestEqual(TEXT("Stop cancels pending hit"), B->GetHealth()->GetHealth(), 120.0f);
				URTSOrderLibrary::IssueAttackOrder({A.Get()}, B.Get());
				NextStage(World);
			}
			else if (Stage == 2 && Elapsed(World, 0.1))
			{
				Test->TestTrue(TEXT("Attack order starts animation"), A->GetAnimState() == ERTSUnitAnimState::Attack);
				URTSOrderLibrary::IssueMoveOrder(A.Get(), {A.Get()}, A->GetActorLocation() + FVector(0, -500, 0));
				NextStage(World);
			}
			else if (Stage == 3 && Elapsed(World, 0.6))
			{
				Test->TestEqual(TEXT("Move cancels pending hit"), B->GetHealth()->GetHealth(), 120.0f);
				Test->TestTrue(TEXT("Move not replaced by attack"), A->GetCurrentOrder().Type == ERTSOrderType::Move);
				Test->TestTrue(TEXT("Living pawn collision preserved"), A->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
				// A fresh attacker beside B checks death during wind-up without moving fixture units.
				URTSUnitData* Data = A->GetUnitData();
				const FTransform Transform(B->GetActorLocation() + FVector(0, 120, 0));
				ARTSUnit* Dying = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Dying->Configure(Data, 0); Dying->FinishSpawning(Transform);
				Test->TestTrue(TEXT("Second attack begins"), Dying->GetAttack()->TryStartAttack(B.Get()));
				Dying->GetHealth()->ApplyDamage(1000, B.Get());
				Test->TestTrue(TEXT("Death animation state"), Dying->GetAnimState() == ERTSUnitAnimState::Death);
				NextStage(World);
			}
			else if (Stage == 4 && Elapsed(World, 0.5))
			{
				Test->TestEqual(TEXT("Death cancels pending hit"), B->GetHealth()->GetHealth(), 120.0f);
				return true;
			}
			return false;
		}
		bool CheckProjectile(UWorld* World)
		{
			if (Stage == 0)
			{
				Test->TestTrue(TEXT("Ranged attack begins"), A->GetAttack()->TryStartAttack(B.Get()));
				NextStage(World);
			}
			else if (Stage == 1 && Elapsed(World, 0.5))
			{
				Test->TestEqual(TEXT("Flight does not deal instant damage"), B->GetHealth()->GetHealth(), 120.0f);
				bool bProjectile = false;
				for (TActorIterator<ARTSProjectile> It(World); It; ++It) { bProjectile = true; }
				Test->TestTrue(TEXT("Visible projectile actor in flight"), bProjectile);
				NextStage(World);
			}
			else if (Stage == 2 && Elapsed(World, 2.5))
			{
				Test->TestEqual(TEXT("Projectile deals one hit on arrival"), B->GetHealth()->GetHealth(), 110.0f);
				AActor* Wall = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
				Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(100, 20, 150));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
				Wall->SetActorLocation((A->GetActorLocation() + B->GetActorLocation()) * 0.5);
				Blocker = Wall;
				Test->TestFalse(TEXT("Wall blocks line of sight"), A->GetAttack()->HasLineOfSight(B.Get()));
				Test->TestFalse(TEXT("Cannot shoot through wall"), A->GetAttack()->TryStartAttack(B.Get()));
				Wall->Destroy();
				Test->TestTrue(TEXT("Shot starts after removing wall"), A->GetAttack()->TryStartAttack(B.Get()));
				NextStage(World);
			}
			else if (Stage == 3 && Elapsed(World, 0.5))
			{
				// Change allegiance while the projectile is in flight: no delayed friendly hit.
				B->SetGenericTeamId(FGenericTeamId(0));
				NextStage(World);
			}
			else if (Stage == 4 && Elapsed(World, 2.5))
			{
				Test->TestEqual(TEXT("In-flight shot rejects friendly target"), B->GetHealth()->GetHealth(), 110.0f);
				return true;
			}
			return false;
		}
		bool CheckAcquisition(UWorld* World)
		{
			if (Stage == 0)
			{
				A->GetUnitData()->bAutoAcquireTargets = true;
				A->GetUnitData()->AutoChaseLeash = 500;
				URTSOrderLibrary::IssueStopOrder({A.Get()});
				NextStage(World);
			}
			else if (Stage == 1 && Elapsed(World, 0.5))
			{
				Test->TestTrue(TEXT("Stop temporarily suppresses acquisition"), A->GetCurrentOrder().Type == ERTSOrderType::None);
				NextStage(World);
			}
			else if (Stage == 2 && Elapsed(World, 0.6))
			{
				Test->TestTrue(TEXT("Idle unit automatically attacks enemy"), A->GetCurrentOrder().Type == ERTSOrderType::Attack && A->GetCurrentOrder().TargetActor == B);
				A->GetUnitData()->AutoChaseLeash = 50;
				NextStage(World);
			}
			else if (Stage == 3 && Elapsed(World, 0.3))
			{
				Test->TestTrue(TEXT("Automatic pursuit stops beyond leash"), A->GetCurrentOrder().Type == ERTSOrderType::None);
				NextStage(World);
			}
			else if (Stage == 4 && Elapsed(World, 1.0))
			{
				Test->TestTrue(TEXT("Leash prevents repeated reacquisition outside original area"), A->GetCurrentOrder().Type == ERTSOrderType::None);
				URTSOrderLibrary::IssueMoveOrder(A.Get(), {A.Get()}, A->GetActorLocation() + FVector(0, -500, 0));
				NextStage(World);
			}
			else if (Stage == 5 && Elapsed(World, 0.9))
			{
				Test->TestTrue(TEXT("Explicit move keeps priority over acquisition"), A->GetCurrentOrder().Type == ERTSOrderType::Move);
				return true;
			}
			return false;
		}
		bool CheckBattle(UWorld* World)
		{
			int32 Alive[2] = {0, 0};
			for (const TWeakObjectPtr<ARTSUnit>& Weak : Units)
			{
				const ARTSUnit* Unit = Weak.Get();
				if (!Unit) { continue; }
				if (Unit->IsAlive())
				{
					++Alive[Unit->GetTeam()];
					if (Unit->GetCurrentOrder().Type == ERTSOrderType::Attack) { AcquiredTeams.Add(Unit->GetTeam()); }
					if (Unit->GetAnimState() == ERTSUnitAnimState::Attack)
					{
						if (Unit->GetUnitData()->bRangedAttack) { bRanged = true; } else { bMelee = true; }
					}
					if (Unit->GetHealth()->GetHealthPercent() < 1) { DamagedTeams.Add(Unit->GetTeam()); }
				}
				else
				{
					bDeath = true;
					Test->TestTrue(TEXT("Death state and deselection"), Unit->GetAnimState() == ERTSUnitAnimState::Death && !Unit->IsSelected());
				}
			}
			for (TActorIterator<ARTSProjectile> It(World); It; ++It) { bFlight = true; }
			if (Elapsed(World, 1) && (Alive[0] == 0 || Alive[1] == 0))
			{
				Test->TestEqual(TEXT("Both teams automatically acquired enemies"), AcquiredTeams.Num(), 2);
				Test->TestEqual(TEXT("Both teams took damage"), DamagedTeams.Num(), 2);
				Test->TestTrue(TEXT("Melee, ranged, projectile and death observed"), bMelee && bRanged && bFlight && bDeath);
				Test->AddInfo(FString::Printf(TEXT("Automatic battle completed in %.2fs; survivors player=%d enemy=%d; melee/ranged/projectile/death observed."), World->GetTimeSeconds() - StageTime, Alive[0], Alive[1]));
				return true;
			}
			if (Elapsed(World, 90))
			{
				Test->AddError(FString::Printf(TEXT("Battle did not finish: player=%d enemy=%d."), Alive[0], Alive[1])); return true;
			}
			return false;
		}
		FAutomationTestBase* Test;
		int32 Mode;
		int32 Stage = -1;
		double StageTime = 0;
		double WaitStarted = FPlatformTime::Seconds();
		TWeakObjectPtr<ARTSUnit> A, B, Friend;
		TWeakObjectPtr<AActor> Blocker;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		TSet<uint8> AcquiredTeams, DamagedTeams;
		bool bMelee = false, bRanged = false, bFlight = false, bDeath = false;
	};

	void QueueCombatTest(FAutomationTestBase* Test, int32 Mode)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckCombat(Test, Mode));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}

bool FRTSCombatRulesTest::RunTest(const FString&) { QueueCombatTest(this, 0); return true; }
bool FRTSProjectileTest::RunTest(const FString&) { QueueCombatTest(this, 1); return true; }
bool FRTSBattleTest::RunTest(const FString&) { QueueCombatTest(this, 2); return true; }
bool FRTSAcquisitionTest::RunTest(const FString&) { QueueCombatTest(this, 3); return true; }
#endif
