#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "Orders/RTSOrderLibrary.h"
#include "Match/RTSMatchSubsystem.h"
#include "AIController.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Orders/RTSLocalFormationPath.h"
#include "Tests/RTSMovementContinuity.h"
#include "Tests/RTSFormationTrace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCompactFormationTest, "MiniRTS.Movement.CompactSingleType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCompactMixedFormationTest, "MiniRTS.Movement.CompactMixedTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
class FCheckCompactFormation : public IAutomationLatentCommand
{
public:
	explicit FCheckCompactFormation(FAutomationTestBase* InTest, bool bInMixed = false) : Test(InTest), bMixed(bInMixed) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || !World->HasBegunPlay())
		{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("Compact fixture PIE unavailable")); return true; }
		const double Now = World->GetTimeSeconds();
		if (Units.Num()) { Trace.Observe(Units, Now); }
		if (Stage == 0)
		{
			if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
			URTSUnitData* Data = Balance ? Balance->CreateUnitData(TEXT("Swordman"), World) : nullptr;
			if (!Data) { Test->AddError(TEXT("Compact fixture data missing")); return true; }
			URTSUnitData* Archer = bMixed && Balance ? Balance->CreateUnitData(TEXT("Archer"), World) : Data;
			if (!Archer) { Test->AddError(TEXT("Compact archer data missing")); return true; }
			auto Wall = [&](FVector Location, FVector Extent)
			{
				AActor* Actor = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
				Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
				Box->SetBoxExtent(Extent); Box->SetCollisionProfileName(TEXT("BlockAll"));
				Box->SetCanEverAffectNavigation(true); Box->RegisterComponent(); Actor->SetActorLocation(Location);
			};
			// 170cm-wide pocket: normal 144cm slot spacing does not fit at the click.
			Wall(Goal + FVector(-140, -145, 120), FVector(350, 60, 120));
			Wall(Goal + FVector(-140, 145, 120), FVector(350, 60, 120));
			Wall(Goal + FVector(270, 0, 120), FVector(60, 205, 120));
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const FTransform Transform(Goal + FVector(-850 + (Index / 2) * 110, (Index % 2 - 0.5) * 110, 82));
				ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Unit) { Test->AddError(TEXT("Compact unit spawn failed")); return true; }
				Unit->Configure(bMixed && Index % 2 ? Archer : Data, 0); Unit->FinishSpawning(Transform); Units.Add(Unit);
			}
			Trace.Start();
			SetupAt = Now; Stage = 1; return false;
		}
		if (Stage == 1)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (Now - SetupAt < 2 || !Nav || Nav->IsNavigationBuildInProgress())
			{ if (Now - SetupAt > 20) { Test->AddError(TEXT("Compact NavMesh timeout")); return true; } return false; }
			URTSOrderLibrary::IssueMoveOrder(World, Units, Goal); IssuedAt = Now; Stage = 2; return false;
		}
		if (Stage == 3)
		{
			bool bRestored = true;
			for (int32 Index = 0; Index < Units.Num(); ++Index)
			{
				const FVector Target = Goal + FVector(-550 + Index * 130, -1100, 10);
				bRestored &= FVector::Dist2D(Units[Index]->GetActorLocation(), Target) < 20
					&& FMath::IsNearlyEqual(Units[Index]->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Units[Index]->GetStandingCollisionRadius());
			}
			if (bRestored) { Test->AddInfo(TEXT("Compact formation fits at click, no same-group yields; radius restored after departure.")); return true; }
			if (Now - IssuedAt > 30) { Test->AddError(TEXT("Compact departure did not restore collision radius")); return true; }
			return false;
		}
		bool bDone = true;
		if (bMixed && !Handoff.Observe(Units)) { Test->AddError(FString::Printf(TEXT("Rear units still far away when front type settled: %.1fcm"), Handoff.RearDistance)); return true; }
		TSet<int32> Slots;
		for (ARTSUnit* Unit : Units)
		{
			if (!Continuity.Observe(Unit, Now))
			{ Test->AddError(FString::Printf(TEXT("Compact visible pause exceeds 2s: %s slot=%d position=%s"), *Unit->GetName(), Unit->GetFormationSlotIndex(), *Unit->GetActorLocation().ToCompactString())); return true; }
			const FRTSMoveGroup* Group = Unit->GetMoveGroup();
			const int32 Slot = Unit->GetFormationSlotIndex();
			if (!Group) { Test->AddError(TEXT("Compact destination rejected")); return true; }
			if (Group->bNavigationFailed) { Test->AddError(TEXT("Compact layout could not fit")); return true; }
			if (Unit->GetTrafficYieldCount() != 0) { Test->AddError(TEXT("Same formation performed visible yield")); return true; }
			if (Unit->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
				|| Unit->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block)
			{ Test->AddError(TEXT("Compact formation disabled collision")); return true; }
			for (const ARTSUnit* Other : Units)
			{
				if (Other == Unit) { continue; }
				const bool bPassing = Unit->IsTrafficPassing(Other);
				if (bPassing != Other->IsTrafficPassing(Unit)
					|| bPassing != Unit->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Other))
				{ Test->AddError(TEXT("Compact pair ignore is asymmetric or unregistered")); return true; }
				if (!bPassing && FVector::Dist2D(Unit->GetActorLocation(), Other->GetActorLocation())
					< Unit->GetSimpleCollisionRadius() + Other->GetSimpleCollisionRadius() - 1)
				{ Test->AddError(TEXT("Compact capsules physically overlapped outside pair window")); return true; }
			}
			if (Slot == INDEX_NONE) { bDone = false; continue; }
			if (Slots.Contains(Slot)) { Test->AddError(TEXT("Compact slots duplicated")); return true; } Slots.Add(Slot);
			if (Group->SlotUnitTypes[Slot] != Unit->GetFormationUnitType()) { Test->AddError(TEXT("Compact slot type mismatch")); return true; }
			bDone &= Group->bCompactLayout && FVector::Dist2D(Group->Center, Goal) < 20
				&& FVector::Dist2D(Unit->GetActorLocation(), Group->Slots[Slot]) <= 15.1
				&& Unit->GetCurrentOrder().Type == ERTSOrderType::None && Unit->GetVelocity().SizeSquared2D() <= 1
				&& Unit->GetCapsuleComponent()->GetMoveIgnoreActors().IsEmpty()
				&& FMath::IsNearlyEqual(Unit->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Unit->GetFormationCollisionRadius());
		}
		if (!bDone) { StableAt = -1; }
		else if (StableAt < 0) { StableAt = Now; }
		if (bDone && Now - StableAt >= 2)
		{
			Test->AddInfo(FString::Printf(TEXT("Longest compact pre-arrival pause %.2fs"), Continuity.LongestPause));
			if (bMixed) { Test->TestTrue(TEXT("Mixed handoff observed"), Handoff.bMeasured); Test->AddInfo(FString::Printf(TEXT("Rear distance at front completion %.1fcm"), Handoff.RearDistance)); }
			Test->AddInfo(FString::Printf(TEXT("Compact spacing %.1fcm, center error %.1fcm, all 8 settled."), Units[0]->GetMoveGroup()->LaneSpacing, FVector::Dist2D(Units[0]->GetMoveGroup()->Center, Goal)));
			for (int32 Index = 0; Index < Units.Num(); ++Index)
			{
				FRTSOrder Order; Order.Type = ERTSOrderType::Move; Order.TargetLocation = Goal + FVector(-550 + Index * 130, -1100, 10);
				Units[Index]->ReceiveOrder(Order);
			}
			IssuedAt = Now; Stage = 3; return false;
		}
		if (Now - IssuedAt > 60)
		{
			for (int32 Index = 0; Index < Units.Num(); ++Index)
			{
				const ARTSUnit* Unit = Units[Index];
				const int32 Slot = Unit->GetFormationSlotIndex();
				const FVector Target = Slot != INDEX_NONE && Unit->GetMoveGroup() ? Unit->GetMoveGroup()->Slots[Slot] : FVector::ZeroVector;
				TArray<FVector> LocalPath;
				const bool bLocalPath = RTSLocalFormationPath::Find(Unit, Unit->GetCurrentOrder().TargetLocation, LocalPath);
				const AAIController* AI = Cast<AAIController>(Unit->GetController());
				const URTSCrowdFollowingComponent* Crowd = AI ? Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()) : nullptr;
				Test->AddInfo(FString::Printf(TEXT("Unit %d location=%s slot=%d target=%s order=%d velocity=%.1f goal=%s path=%s settling=%d local=%d points=%d passes=%u ignores=%d"), Index,
					*Unit->GetActorLocation().ToCompactString(), Slot, *Target.ToCompactString(), static_cast<int32>(Unit->GetCurrentOrder().Type), Unit->GetVelocity().Size2D(),
					*Unit->GetCurrentOrder().TargetLocation.ToCompactString(), Crowd ? *Crowd->GetCurrentTargetLocation().ToCompactString() : TEXT("none"),
					Crowd && Crowd->IsFormationSettling(), bLocalPath, LocalPath.Num(), Unit->GetTrafficPassCount(), Unit->GetCapsuleComponent()->GetMoveIgnoreActors().Num()));
			}
			Test->AddError(TEXT("Compact formation failed to settle at requested destination")); return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	bool bMixed = false;
	TArray<ARTSUnit*> Units;
	FRTSMovementContinuity Continuity;
	FRTSFormationHandoff Handoff;
	RTSFormationTrace::FTrace Trace;
	FVector Goal = FVector(500, 500, 0);
	int32 Stage = 0;
	double Started = FPlatformTime::Seconds(), SetupAt = 0, IssuedAt = 0, StableAt = -1;
};
}
bool FRTSCompactFormationTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckCompactFormation(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
bool FRTSCompactMixedFormationTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckCompactFormation(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
