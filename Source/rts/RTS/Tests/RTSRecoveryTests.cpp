#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealthComponent.h"
#include "Units/RTSUnit.h"
#include "Orders/RTSOrderLibrary.h"
#include "Orders/RTSMovementProgress.h"
#include "Match/RTSMatchSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFriendlyYieldTest, "MiniRTS.Congestion.FriendlyYield", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldPolicyTest, "MiniRTS.Congestion.YieldPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldPriorityTest, "MiniRTS.Congestion.YieldOrderPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldReturnTest, "MiniRTS.Congestion.YieldFormationReturn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldCancelTest, "MiniRTS.Congestion.YieldRequesterCancelled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldDeathTest, "MiniRTS.Congestion.YieldRequesterDied", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldGuardTest, "MiniRTS.Congestion.YieldGuardInterrupt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProgressTest, "MiniRTS.Congestion.ProgressAgainstOscillation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSYieldOffsetTest, "MiniRTS.Congestion.YieldMinimalOffset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	void Move(ARTSUnit* Unit, const FVector& Goal)
	{
		FRTSOrder Order; Order.Type = ERTSOrderType::Move; Order.TargetLocation = Goal; Unit->ReceiveOrder(Order);
	}

	class FCheckYield : public IAutomationLatentCommand
	{
	public:
		FCheckYield(FAutomationTestBase* InTest, int32 InMode) : Test(InTest), Mode(InMode) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			const double Now = World->GetTimeSeconds();
			if (Stage == 0)
			{
				if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
				URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
				URTSUnitData* Data = Balance ? Balance->CreateUnitData(TEXT("Swordman"), World) : nullptr;
				if (!Data) { Test->AddError(TEXT("Swordman data unavailable")); return true; }
				auto Spawn = [&](FVector Location)
				{
					const FTransform Transform(Location);
					ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					if (Unit) { Unit->Configure(Data, 0); Unit->FinishSpawning(Transform); }
					return Unit;
				};
				Requester = Spawn(FVector(0, -120, 82)); Blocker = Spawn(FVector(Mode == 7 ? 45 : 0, 0, 82));
				if (!Requester || !Blocker) { Test->AddError(TEXT("Traffic fixture spawn failed")); return true; }
				Speed = Requester->GetCharacterMovement()->MaxWalkSpeed;
				SetupAt = Now; Stage = 1; return false;
			}
			if (!CheckCollision()) { return true; }
			if (Stage == 1)
			{
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (Now - SetupAt < 2 || !Nav || Nav->IsNavigationBuildInProgress())
				{ if (Now - SetupAt > 15) { Test->AddError(TEXT("Traffic fixture NavMesh timeout")); return true; } return false; }
				if (Mode == 3 || Mode == 4 || Mode == 5)
				{
					URTSOrderLibrary::IssueMoveOrder(World, {Blocker}, FVector::ZeroVector);
					Stage = 2; return false;
				}
				BeginRequest(Now); Stage = 3; return false;
			}
			if (Stage == 2)
			{
				if (Blocker->GetCurrentOrder().Type == ERTSOrderType::None && Blocker->GetMoveGroup() && Blocker->GetFormationSlotIndex() != INDEX_NONE)
				{
					ReservedGroup = Blocker->GetMoveGroup(); ReservedSlot = Blocker->GetFormationSlotIndex();
					SlotLocation = ReservedGroup->Slots[ReservedSlot];
					RequestStart = FVector(Blocker->GetActorLocation().X, Blocker->GetActorLocation().Y - 120, 10);
					Move(Requester, RequestStart); Stage = 4; return false;
				}
				if (Now - SetupAt > 15) { Test->AddError(TEXT("Blocker never acquired its fixture slot")); return true; }
				return false;
			}
			if (Stage == 4)
			{
				if (Requester->GetCurrentOrder().Type == ERTSOrderType::None && FVector::Dist2D(Requester->GetActorLocation(), RequestStart) < 20)
				{ BeginRequest(Now); Stage = 3; return false; }
				if (Now - SetupAt > 20) { Test->AddError(TEXT("Requester could not reach the reserved slot's approach")); return true; }
				return false;
			}
			if (ReservedGroup && (Blocker->GetMoveGroup() != ReservedGroup || Blocker->GetFormationSlotIndex() != ReservedSlot || !ReservedGroup->Claimed[ReservedSlot]))
			{ Test->AddError(TEXT("Yield released or changed the reserved formation slot")); return true; }
			if (Mode == 1) { return CheckPolicy(); }
			if (!bSawYield && Blocker->GetTrafficYieldCount() > 0)
			{
				bSawYield = true;
				Requester->GetCharacterMovement()->MaxWalkSpeed = Speed;
				if (Mode == 2)
				{
					Move(Blocker, FVector(-450, 0, 10));
					if (!Test->TestFalse(TEXT("New explicit order cancels yield immediately"), Blocker->IsYieldingToTraffic())) { return true; }
				}
				if (Mode == 4) { URTSOrderLibrary::IssueStopOrder({Requester}); }
				if (Mode == 5) { Requester->GetHealth()->ApplyDamage(Requester->GetHealth()->GetMaxHealth(), Blocker); }
				if (Mode == 6)
				{
					Blocker->SetCombatStance(ERTSCombatStance::Guard);
					if (!Test->TestFalse(TEXT("Guard immediately cancels traffic movement"), Blocker->IsYieldingToTraffic())) { return true; }
					URTSOrderLibrary::IssueStopOrder({Requester});
				}
			}
			MaxSideStep = FMath::Max(MaxSideStep, FMath::Abs(Blocker->GetActorLocation().X - BlockerOrigin.X));
			const bool bRequesterArrived = FVector::Dist2D(Requester->GetActorLocation(), RequestGoal) < 20
				&& Requester->GetCurrentOrder().Type == ERTSOrderType::None;
			const bool bRequestEnded = Mode >= 4 ? Requester->GetCurrentOrder().Type == ERTSOrderType::None : bRequesterArrived;
			bool bDone = bSawYield && bRequestEnded && !Blocker->IsYieldingToTraffic()
				&& Requester->GetVelocity().SizeSquared2D() <= 1 && Blocker->GetVelocity().SizeSquared2D() <= 1;
			if (Mode == 2) { bDone &= FVector::Dist2D(Blocker->GetActorLocation(), FVector(-450, 0, 10)) < 20 && Blocker->GetCurrentOrder().Type == ERTSOrderType::None; }
			if (Mode == 3 || Mode == 4 || Mode == 5) { bDone &= ReservedGroup->Settled[ReservedSlot] && FVector::Dist2D(Blocker->GetActorLocation(), SlotLocation) <= 15.1 && Blocker->GetCurrentOrder().Type == ERTSOrderType::None; }
			if (bDone)
			{
				if (SettledAt < 0) { SettledAt = Now; }
				if (Now - SettledAt < 2) { return false; }
				if (Mode == 0 || Mode == 3)
				{
					Test->TestTrue(TEXT("Blocker physically sidestepped a meaningful distance"), MaxSideStep > 50);
					Test->TestTrue(TEXT("Yield remains within one restrained sidestep"), MaxSideStep <= 100);
				}
				if (Mode == 7) { Test->TestTrue(TEXT("Offset blocker only supplies the missing lateral clearance"), MaxSideStep > 5 && MaxSideStep < 40); }
				Test->AddInfo(FString::Printf(TEXT("Traffic mode %d: request completed or cancelled, blocker yields=%u maxSidestep=%.1fcm; stable for 2s, collisions never ignored."), Mode, Blocker->GetTrafficYieldCount(), MaxSideStep));
				return true;
			}
			SettledAt = -1;
			if (Now - IssuedAt > 25)
			{
				Requester->GetCharacterMovement()->MaxWalkSpeed = Speed;
				Test->AddError(FString::Printf(TEXT("Traffic mode %d stalled: requester=%s order=%d blocker=%s order=%d yield=%d count=%u side=%.1f"), Mode,
					*Requester->GetActorLocation().ToCompactString(), static_cast<int32>(Requester->GetCurrentOrder().Type),
					*Blocker->GetActorLocation().ToCompactString(), static_cast<int32>(Blocker->GetCurrentOrder().Type), Blocker->IsYieldingToTraffic(), Blocker->GetTrafficYieldCount(), MaxSideStep)); return true;
			}
			return false;
		}
	private:
		void BeginRequest(double Now)
		{
			BlockerOrigin = Blocker->GetActorLocation();
			RequestGoal = FVector(Mode == 7 ? Requester->GetActorLocation().X : BlockerOrigin.X, BlockerOrigin.Y + 500, 10);
			Requester->GetCharacterMovement()->MaxWalkSpeed = 0;
			Move(Requester, RequestGoal); IssuedAt = Now;
		}
		bool CheckCollision()
		{
			if (!Requester->IsAlive()) { return true; } // Death intentionally disables its capsule.
			const UCapsuleComponent* A = Requester->GetCapsuleComponent(); const UCapsuleComponent* B = Blocker->GetCapsuleComponent();
			if (A->GetMoveIgnoreActors().Contains(Blocker) || B->GetMoveIgnoreActors().Contains(Requester)
				|| A->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block || B->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
				|| A->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block || B->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block
				|| FVector::Dist2D(Requester->GetActorLocation(), Blocker->GetActorLocation()) < Requester->GetSimpleCollisionRadius() + Blocker->GetSimpleCollisionRadius() - 1)
			{ Test->AddError(TEXT("Traffic fixture lost physical collision or capsules overlapped")); return false; }
			return true;
		}
		bool CheckPolicy()
		{
			Blocker->SetCombatStance(ERTSCombatStance::Guard);
			Test->TestFalse(TEXT("Guard refuses yield"), Blocker->TryYieldTo(Requester, FVector::RightVector));
			Blocker->SetCombatStance(ERTSCombatStance::Patrol);
			Blocker->SetGenericTeamId(FGenericTeamId(1));
			Test->TestFalse(TEXT("Enemy refuses yield"), Blocker->TryYieldTo(Requester, FVector::RightVector));
			Blocker->SetGenericTeamId(FGenericTeamId(0));
			FRTSOrder Work; Work.Type = ERTSOrderType::Build; Blocker->ReceiveOrder(Work);
			Test->TestFalse(TEXT("Work order refuses yield"), Blocker->TryYieldTo(Requester, FVector::RightVector));
			Test->TestTrue(TEXT("Work order retained"), Blocker->GetCurrentOrder().Type == ERTSOrderType::Build);
			Move(Blocker, FVector(450, 0, 10));
			Test->TestFalse(TEXT("Active move refuses yield"), Blocker->TryYieldTo(Requester, FVector::RightVector));
			Test->TestEqual(TEXT("Refused requests caused no yields"), Blocker->GetTrafficYieldCount(), uint32(0));
			Requester->GetCharacterMovement()->MaxWalkSpeed = Speed;
			return true;
		}
		FAutomationTestBase* Test;
		int32 Mode, Stage = 0, ReservedSlot = INDEX_NONE;
		ARTSUnit* Requester = nullptr; ARTSUnit* Blocker = nullptr;
		const FRTSMoveGroup* ReservedGroup = nullptr;
		FVector SlotLocation = FVector::ZeroVector, RequestStart = FVector::ZeroVector;
		FVector BlockerOrigin = FVector::ZeroVector, RequestGoal = FVector::ZeroVector;
		float Speed = 350; double MaxSideStep = 0;
		bool bSawYield = false;
		double Started = FPlatformTime::Seconds(), SetupAt = 0, IssuedAt = 0, SettledAt = -1;
	};
	void RunYield(FAutomationTestBase* Test, int32 Mode)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckYield(Test, Mode));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}
bool FRTSFriendlyYieldTest::RunTest(const FString&) { RunYield(this, 0); return true; }
bool FRTSYieldPolicyTest::RunTest(const FString&) { RunYield(this, 1); return true; }
bool FRTSYieldPriorityTest::RunTest(const FString&) { RunYield(this, 2); return true; }
bool FRTSYieldReturnTest::RunTest(const FString&) { RunYield(this, 3); return true; }
bool FRTSYieldCancelTest::RunTest(const FString&) { RunYield(this, 4); return true; }
bool FRTSYieldDeathTest::RunTest(const FString&) { RunYield(this, 5); return true; }
bool FRTSYieldGuardTest::RunTest(const FString&) { RunYield(this, 6); return true; }
bool FRTSYieldOffsetTest::RunTest(const FString&) { RunYield(this, 7); return true; }
bool FRTSProgressTest::RunTest(const FString&)
{
	FRTSMovementProgress Progress;
	const FVector Goal(100, 0, 0);
	TestFalse(TEXT("Initial sample starts observation"), Progress.Observe(Goal, 100, 0));
	TestFalse(TEXT("Wait before declaring stall"), Progress.Observe(Goal, 105, 0.6));
	TestTrue(TEXT("Circling without net path progress is stalled"), Progress.Observe(Goal, 99, 1.7));
	TestFalse(TEXT("Meaningful progress clears stall"), Progress.Observe(Goal, 80, 1.8));
	TestFalse(TEXT("New destination starts a fresh observation"), Progress.Observe(FVector(200, 0, 0), 100, 4));
	TestFalse(TEXT("Small progress accumulates"), Progress.Observe(FVector(200, 0, 0), 94, 4.8));
	TestFalse(TEXT("Cumulative progress refreshes deadline"), Progress.Observe(FVector(200, 0, 0), 88, 5.5));
	return true;
}
#endif
