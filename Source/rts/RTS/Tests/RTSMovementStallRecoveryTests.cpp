#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "AIController.h"
#include "Match/RTSMatchSubsystem.h"

// Seed only the returning-reservation state; all recovery eligibility, navigation,
// sweeps and real movement still execute. No actor is repositioned after spawning.
struct FRTSMovementStallTestAccess
{
	static bool BeginReturn(ARTSUnit* Unit, const FVector& Slot)
	{
		TSharedRef<FRTSMoveGroup> Group = MakeShared<FRTSMoveGroup>();
		Group->Center = Group->RequestedGoal = Slot;
		Group->Slots.Add(Slot); Group->Claimed.Add(true); Group->Settled.Add(false);
		Group->BuildTypeLayout({Unit->GetFormationUnitType()}, 1);
		if (!Group->PrepareNavigation(Unit->GetWorld())) { return false; }
		Unit->MoveGroup = Group;
		Unit->GroupMember = Group->RegisterMember(Unit->GetFormationUnitType(), Unit->GetActorLocation());
		Unit->GroupSlot = 0;
		Unit->YieldPhase = 3;
		Unit->CurrentOrder.Type = ERTSOrderType::Move;
		Unit->CurrentOrder.TargetLocation = Group->Slots[0];
		Unit->GroupMoveStarted = Unit->GetWorldTimeForTraffic();
		Unit->bGroupApproachStarted = true;
		Unit->UpdateFormationCollision();
		Unit->StopAIMovement();
		if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
		{
			if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent())) { Crowd->SetFormationSettling(true); }
		}
		const bool bRequested = Unit->RequestMoveTo(Group->Slots[0], 5);
		if (bRequested) { Unit->BeginFormationRoute(); }
		return bRequested;
	}
	static void CheckRouteCommit(ARTSUnit* Unit, FAutomationTestBase* Test)
	{
		const double Now = Unit->GetWorldTimeForTraffic();
		const FVector Goal = Unit->CurrentOrder.TargetLocation;
		Unit->FormationProgress.BeginRoute(Goal, 1, Now - 5);
		Unit->TrafficProgress.BeginRoute(Goal, 1, Now - 5);
		Unit->FormationWaypoints = {Unit->GetActorLocation() + FVector(200, 0, 0), Goal};
		Unit->BeginFormationRoute();
		Test->TestEqual(TEXT("New detour rebases formation deadline"), Unit->FormationProgress.LastProgressAt, Now);
		Test->TestEqual(TEXT("New detour rebases traffic deadline"), Unit->TrafficProgress.LastProgressAt, Now);
		Test->TestEqual(TEXT("Observers count the entire detour"), Unit->TrafficProgress.BestRemaining, Unit->GetRemainingFormationDistance());
		Unit->FormationProgress.LastProgressAt = Unit->TrafficProgress.LastProgressAt = Now - 5;
		Unit->BeginFormationRoute();
		Test->TestEqual(TEXT("Same route retry preserves formation stall"), Unit->FormationProgress.LastProgressAt, Now - 5);
		Test->TestEqual(TEXT("Same route retry preserves traffic stall"), Unit->TrafficProgress.LastProgressAt, Now - 5);
		Unit->FormationWaypoints.Reset(); Unit->FormationRouteSnapshot.Reset();
		Unit->BeginFormationRoute();
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSReturnSlotRecoveryTest, "MiniRTS.Congestion.ReturnSlotYield", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckReturnSlotRecovery : public IAutomationLatentCommand
	{
	public:
		explicit FCheckReturnSlotRecovery(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{
				if (FPlatformTime::Seconds() - Started < 30) { return false; }
				Test->AddError(TEXT("Return-slot fixture PIE unavailable")); return true;
			}
			const double Now = World->GetTimeSeconds();
			if (Stage == 0)
			{
				if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
				URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
				URTSUnitData* Data = Balance ? Balance->CreateUnitData(TEXT("Swordman"), World) : nullptr;
				if (!Data) { Test->AddError(TEXT("Return-slot fixture data unavailable")); return true; }
				auto Spawn = [&](const FVector& Location)
				{
					const FTransform Transform(Location);
					ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					if (Unit) { Unit->Configure(Data, 0); Unit->FinishSpawning(Transform); }
					return Unit;
				};
				Returning = Spawn(FVector(0, -120, 82)); Blocker = Spawn(FVector(0, 0, 82));
				if (!Returning || !Blocker) { Test->AddError(TEXT("Return-slot fixture spawn failed")); return true; }
				Returning->SetCombatStance(ERTSCombatStance::Guard);
				SetupAt = Now; Stage = 1; return false;
			}
			if (Stage == 1)
			{
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (Now - SetupAt < 2 || !Nav || Nav->IsNavigationBuildInProgress())
				{
					if (Now - SetupAt > 20) { Test->AddError(TEXT("Return-slot fixture navigation unavailable")); return true; }
					return false;
				}
				Speed = Returning->GetCharacterMovement()->MaxWalkSpeed;
				Returning->GetCharacterMovement()->MaxWalkSpeed = 0; // Deterministic stall; restored once recovery is observed.
				if (!FRTSMovementStallTestAccess::BeginReturn(Returning, FVector(0, 250, 10)))
				{ Test->AddError(TEXT("Return-slot fixture could not request a path")); return true; }
				FRTSMovementStallTestAccess::CheckRouteCommit(Returning, Test);
				ReservedGroup = Returning->GetMoveGroup();
				Radius = Returning->GetCapsuleComponent()->GetScaledCapsuleRadius();
				IssuedAt = Now; Stage = 2; return false;
			}
			if (Returning->GetMoveGroup() != ReservedGroup || Returning->GetFormationSlotIndex() != 0 || !ReservedGroup->Claimed[0])
			{ Test->AddError(TEXT("Return recovery released its reservation")); return true; }
			for (ARTSUnit* Unit : {Returning, Blocker})
			{
				const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent();
				if (!Capsule->GetMoveIgnoreActors().IsEmpty() || Capsule->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
					|| Capsule->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block)
				{ Test->AddError(TEXT("Return recovery changed physical collision")); return true; }
			}
			if (Returning->GetCapsuleComponent()->GetScaledCapsuleRadius() != Radius
				|| FVector::Dist2D(Returning->GetActorLocation(), Blocker->GetActorLocation()) < Returning->GetSimpleCollisionRadius() + Blocker->GetSimpleCollisionRadius() - 1)
			{ Test->AddError(TEXT("Return recovery shrank or overlapped capsules")); return true; }
			if (!bSawYield && Blocker->GetTrafficYieldCount() > 0)
			{
				bSawYield = true;
				Returning->GetCharacterMovement()->MaxWalkSpeed = Speed;
			}
			const bool bDone = bSawYield && ReservedGroup->Settled[0] && Returning->GetCurrentOrder().Type == ERTSOrderType::None
				&& !Returning->IsRecoveringFromCongestion() && !Blocker->IsYieldingToTraffic()
				&& FVector::Dist2D(Returning->GetActorLocation(), ReservedGroup->Slots[0]) <= 15.1;
			if (!bDone) { StableAt = -1; }
			else if (StableAt < 0) { StableAt = Now; }
			else if (Now - StableAt >= 2) { Test->AddInfo(TEXT("Returning member requested a physical sidestep and settled in the original reserved slot; stable for 2s.")); return true; }
			if (Now - IssuedAt > 25)
			{
				Returning->GetCharacterMovement()->MaxWalkSpeed = Speed;
				Test->AddError(FString::Printf(TEXT("Return-slot recovery stalled: yielded=%d return=%s blocker=%s"), bSawYield,
					*Returning->GetActorLocation().ToCompactString(), *Blocker->GetActorLocation().ToCompactString())); return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		ARTSUnit* Returning = nullptr;
		ARTSUnit* Blocker = nullptr;
		const FRTSMoveGroup* ReservedGroup = nullptr;
		int32 Stage = 0;
		float Speed = 0, Radius = 0;
		bool bSawYield = false;
		double Started = FPlatformTime::Seconds(), SetupAt = 0, IssuedAt = 0, StableAt = -1;
	};
}
bool FRTSReturnSlotRecoveryTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckReturnSlotRecovery(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
