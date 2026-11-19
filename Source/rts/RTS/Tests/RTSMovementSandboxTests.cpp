#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Units/RTSUnit.h"
#include "Tests/RTSMovementContinuity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMovementSandboxCommandsTest, "MiniRTS.Movement.ManualScenarioCommands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
class FCheckMovementSandboxCommands : public IAutomationLatentCommand
{
public:
	explicit FCheckMovementSandboxCommands(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || !World->HasBegunPlay())
		{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("Manual command fixture PIE unavailable")); return true; }
		const double Now = World->GetTimeSeconds();
		auto Command = [&](const TCHAR* Name)
		{ Test->TestTrue(FString::Printf(TEXT("Command registered: %s"), Name), IConsoleManager::Get().ProcessUserConsoleInput(Name, *GLog, World)); };
		TArray<ARTSUnit*> Units;
		for (TActorIterator<ARTSUnit> It(World); It; ++It)
		{ if (It->ActorHasTag(TEXT("RTSMovementSandbox"))) { Units.Add(*It); } }
		if (Stage == 0)
		{
			int32 InitialUnits = 0;
			for (TActorIterator<ARTSUnit> It(World); It; ++It) { ++InitialUnits; }
			if (!Test->TestEqual(TEXT("PIE starts without any gameplay units"), InitialUnits, 0)) { return true; }
			if (!Test->TestEqual(TEXT("PIE starts with no test units"), Units.Num(), 0)) { return true; }
			Sentinel = World->SpawnActor<AActor>();
			Command(TEXT("rts.TestCompact")); At = Now; Stage = 1; return false;
		}
		if (Stage == 1)
		{
			if (Now - At < 2) { return false; }
			if (!IdleCount(Units, 8)) { return true; }
			Command(TEXT("rts.TestCompactMixed")); Command(TEXT("rts.TestCompactMixed"));
			At = Now; Stage = 2; return false;
		}
		if (Stage == 2)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (Now - At < 2 || !Nav || Nav->IsNavigationBuildInProgress())
			{ if (Now - At > 20) { Test->AddError(TEXT("Manual command NavMesh timeout")); return true; } return false; }
			if (!IdleCount(Units, 8)) { return true; }
			TMap<FName, int32> Counts;
			for (ARTSUnit* Unit : Units) { ++Counts.FindOrAdd(Unit->GetFormationUnitType()); }
			if (!Test->TestEqual(TEXT("Mixed scene has two unit types"), Counts.Num(), 2)) { return true; }
			for (const auto& Pair : Counts) { Test->TestEqual(TEXT("Four units per mixed type"), Pair.Value, 4); }
			Command(TEXT("rts.TestGo")); At = Now; Stage = 3; return false;
		}
		if (Stage == 3)
		{
			bool bDone = Units.Num() == 8;
			if (!Handoff.Observe(Units)) { Test->AddError(FString::Printf(TEXT("TestGo rear units too far at front completion: %.1fcm"), Handoff.RearDistance)); return true; }
			for (ARTSUnit* Unit : Units)
			{
				if (!Continuity.Observe(Unit, Now))
				{ Command(TEXT("rts.TestStatus")); Test->AddError(TEXT("TestGo mixed scene has a pre-arrival pause longer than 2s")); return true; }
				const FRTSMoveGroup* Group = Unit->GetMoveGroup();
				const int32 Slot = Unit->GetFormationSlotIndex();
				bDone &= Group && Group->bCompactLayout && Slot != INDEX_NONE
					&& Unit->GetCurrentOrder().Type == ERTSOrderType::None && Unit->GetVelocity().SizeSquared2D() <= 1;
				if (Group && Slot != INDEX_NONE) { bDone &= FVector::Dist2D(Unit->GetActorLocation(), Group->Slots[Slot]) <= 15.1; }
			}
			if (!bDone) { StableAt = -1; }
			else if (StableAt < 0) { StableAt = Now; }
			if (bDone && Now - StableAt >= 2)
			{ Test->TestTrue(TEXT("TestGo mixed handoff observed"), Handoff.bMeasured); Test->AddInfo(FString::Printf(TEXT("TestGo rear distance %.1fcm; longest pause %.2fs"), Handoff.RearDistance, Continuity.LongestPause)); Command(TEXT("rts.TestStatus")); Command(TEXT("rts.TestLeave")); At = Now; Stage = 4; return false; }
			if (Now - At > 60)
			{
				Command(TEXT("rts.TestStatus"));
				for (ARTSUnit* Unit : Units)
				{
					const FRTSMoveGroup* Group = Unit->GetMoveGroup();
					if (!Group) { continue; }
					for (int32 Index = 0; Index < Group->Slots.Num(); ++Index)
					{ Test->AddInfo(FString::Printf(TEXT("slot %d type=%s goal=%s claimed=%d settled=%d"), Index, *Group->SlotUnitTypes[Index].ToString(), *Group->Slots[Index].ToCompactString(), Group->Claimed[Index], Group->Settled[Index])); }
					break;
				}
				Test->AddError(TEXT("Manual mixed command scene did not settle")); return true;
			}
			return false;
		}
		if (Stage == 4)
		{
			bool bRestored = Units.Num() == 8;
			for (ARTSUnit* Unit : Units)
			{
				bRestored &= !Unit->GetMoveGroup() && Unit->GetCurrentOrder().Type == ERTSOrderType::None
					&& FMath::IsNearlyEqual(Unit->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Unit->GetStandingCollisionRadius());
			}
			if (bRestored)
			{ Command(TEXT("rts.TestYield")); At = Now; Stage = 5; return false; }
			if (Now - At > 30) { Test->AddError(TEXT("Manual leave command did not restore radius")); return true; }
			return false;
		}
		if (Stage == 5)
		{
			if (Now - At < 2) { return false; }
			if (!IdleCount(Units, 2)) { return true; }
			Command(TEXT("rts.TestGo")); At = Now; Stage = 6; return false;
		}
		if (Stage == 6)
		{
			bool bArrived = false;
			for (ARTSUnit* Unit : Units)
			{
				bArrived |= Unit->ActorHasTag(TEXT("RTSMovementSandboxMover"))
					&& FVector::Dist2D(Unit->GetActorLocation(), FVector(-500, 500, 10)) < 20
					&& Unit->GetCurrentOrder().Type == ERTSOrderType::None && Unit->GetCharacterMovement()->MaxWalkSpeed > 0;
			}
			if (bArrived)
			{ Command(TEXT("rts.TestReset")); At = Now; Stage = 7; return false; }
			if (Now - At > 25) { Test->AddError(TEXT("Manual yield command failed passage/yield/speed restore")); return true; }
			return false;
		}
		if (Stage == 7)
		{
			if (Now - At < 1) { return false; }
			int32 Fixtures = 0;
			for (TActorIterator<AActor> It(World); It; ++It) { Fixtures += It->ActorHasTag(TEXT("RTSMovementSandbox")); }
			Test->TestEqual(TEXT("Reset clears units, walls and markers"), Fixtures, 0);
			Test->TestTrue(TEXT("Reset preserves unrelated actor"), Sentinel.IsValid());
			Test->AddInfo(TEXT("Manual commands: empty startup, idle opt-in scenes, non-stacking reset, mixed compact arrival/leave and friendly yield passed. PIE stayed running until test cleanup."));
			return true;
		}
		return true;
	}
private:
	bool IdleCount(const TArray<ARTSUnit*>& Units, int32 Expected)
	{
		if (!Test->TestEqual(TEXT("Only requested scene units exist"), Units.Num(), Expected)) { return false; }
		for (ARTSUnit* Unit : Units)
		{
			if (Unit->GetCurrentOrder().Type != ERTSOrderType::None || Unit->GetMoveGroup() || Unit->GetVelocity().SizeSquared2D() > 1)
			{ Test->AddError(TEXT("Scene moved without TestGo or player order")); return false; }
		}
		return true;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<AActor> Sentinel;
	FRTSMovementContinuity Continuity;
	FRTSFormationHandoff Handoff;
	int32 Stage = 0;
	double Started = FPlatformTime::Seconds(), At = 0, StableAt = -1;
};
}
bool FRTSMovementSandboxCommandsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckMovementSandboxCommands(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
