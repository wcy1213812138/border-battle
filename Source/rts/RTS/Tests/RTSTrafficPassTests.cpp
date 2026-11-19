#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Combat/RTSHealthComponent.h"
#include "Units/RTSUnit.h"
#include "Orders/RTSOrderLibrary.h"
#include "Match/RTSMatchSubsystem.h"

// Boundary tests bypass only the stall timer, not any eligibility/collision query.
struct FRTSTrafficPassTestAccess
{
	static bool Start(ARTSUnit* A, ARTSUnit* B) { return A->TryTrafficPass(B, FVector::RightVector); }
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSTrafficPassTest, "MiniRTS.Congestion.PairPass", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FRTSTrafficPassTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	const TCHAR* Cases[] = {TEXT("AutomaticFallback"), TEXT("Timeout"), TEXT("RequesterCancelled"), TEXT("BlockerCancelled"),
		TEXT("BlockerDied"), TEXT("BlockerDestroyed"), TEXT("RequesterDestroyed"), TEXT("SafetyBoundaries"), TEXT("CrowdStandOff")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index) { Names.Add(Cases[Index]); Commands.Add(FString::FromInt(Index)); }
}
namespace
{
class FCheckTrafficPass : public IAutomationLatentCommand
{
public:
	FCheckTrafficPass(FAutomationTestBase* InTest, int32 InMode) : Test(InTest), Mode(InMode) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || !World->HasBegunPlay())
		{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("Pair pass PIE unavailable")); return true; }
		const double Now = World->GetTimeSeconds();
		if (Stage == 0)
		{
			if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
			URTSUnitData* Data = Balance ? Balance->CreateUnitData(TEXT("Swordman"), World) : nullptr;
			if (!Data) { Test->AddError(TEXT("Pair pass data unavailable")); return true; }
			auto Spawn = [&](FVector Location)
			{
				const FTransform Transform(Location);
				ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Unit) { Unit->Configure(Data, 0); Unit->FinishSpawning(Transform); Unit->SetCombatStance(ERTSCombatStance::Guard); }
				return Unit;
			};
			A = Spawn(FVector(0, -100, 82)); B = Spawn(FVector(0, 0, 82)); Third = Spawn(FVector(500, 0, 82));
			if (!A || !B || !Third) { Test->AddError(TEXT("Pair pass spawn failed")); return true; }
			SetupAt = Now; Stage = 1; return false;
		}
		if (Stage == 1)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (Now - SetupAt < 2 || !Nav || Nav->IsNavigationBuildInProgress())
			{ if (Now - SetupAt > 20) { Test->AddError(TEXT("Pair pass NavMesh timeout")); return true; } return false; }
			Combined = A->GetSimpleCollisionRadius() + B->GetSimpleCollisionRadius();
			A->SetActorLocation(B->GetActorLocation() - FVector(0, Combined + (Mode == 8 ? 70 : 2), 0));
			A->GetCharacterMovement()->MaxWalkSpeed = Mode == 0 || Mode == 8 ? 1 : 100;
			FRTSOrder Order; Order.Type = ERTSOrderType::Move; Order.TargetLocation = FVector(0, 450, 10); A->ReceiveOrder(Order);
			IssuedAt = Now;
			if (Mode == 7) { return CheckBoundaries(World); }
			if (Mode != 0 && Mode != 8 && !Test->TestTrue(TEXT("Eligible blocked friendly pair starts"), FRTSTrafficPassTestAccess::Start(A, B))) { return true; }
			Stage = 2; return false;
		}
		if (!CheckChannels()) { return true; }
		if (IsValid(A) && A->IsTrafficPassing(B))
		{
			if (!bSawPass)
			{
				bSawPass = true; PassAt = Now;
				if (!Test->TestTrue(TEXT("Pair ignore is symmetric"), B->IsTrafficPassing(A)
					&& A->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(B) && B->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(A))) { return true; }
				if (Mode == 0 || Mode == 8) { A->GetCharacterMovement()->MaxWalkSpeed = 350; }
			}
			if (!bInterrupted && Mode > 0 && Mode < 7 && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) < Combined - 8)
			{
				bInterrupted = true;
				switch (Mode)
				{
				case 1: A->GetCharacterMovement()->MaxWalkSpeed = 0; break;
				case 2: URTSOrderLibrary::IssueStopOrder({A}); break;
				case 3: URTSOrderLibrary::IssueStopOrder({B}); break;
				case 4: B->GetHealth()->ApplyDamage(B->GetHealth()->GetMaxHealth(), A); break;
				case 5: B->Destroy(); break;
				case 6: A->Destroy(); break;
				}
			}
		}
		if (bSawPass && (!IsValid(A) || !IsValid(B) || !A->IsTrafficPassing(B)))
		{
			if (!bEnded)
			{
				bEnded = true;
				Test->TestTrue(TEXT("Exception lasts at most two seconds"), Now - PassAt <= 2.2);
				if (Mode == 1) { Test->TestTrue(TEXT("Timeout actually elapsed"), Now - PassAt >= 1.8); A->GetCharacterMovement()->MaxWalkSpeed = 350; URTSOrderLibrary::IssueStopOrder({A}); }
				if (Mode > 0 && Mode < 7) { Test->TestTrue(TEXT("Lifecycle interruption occurred while overlapping"), bInterrupted); }
				if (Mode == 2) { Test->TestTrue(TEXT("Stop is not replaced by the original order"), A->GetCurrentOrder().Type == ERTSOrderType::None); }
			}
			for (ARTSUnit* Unit : {A, B, Third})
			{
				if (IsValid(Unit) && !Test->TestTrue(TEXT("Surviving ignore lists restored"), Unit->GetCapsuleComponent()->GetMoveIgnoreActors().IsEmpty())) { return true; }
			}
			const bool bSeparated = !IsValid(A) || !IsValid(B) || !A->IsAlive() || !B->IsAlive()
				|| FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) >= Combined - 1;
			const bool bArrived = (Mode != 0 && Mode != 8) || (A->GetCurrentOrder().Type == ERTSOrderType::None && FVector::Dist2D(A->GetActorLocation(), FVector(0, 450, 10)) < 20);
			if (bSeparated && bArrived)
			{
				if (StableAt < 0) { StableAt = Now; }
				if (Now - StableAt >= 2) { Test->AddInfo(FString::Printf(TEXT("Pair case %d: real pass observed; restored and separated for 2s."), Mode)); return true; }
			}
			else { StableAt = -1; }
		}
		if (Now - IssuedAt > 16)
		{
			Test->AddError(FString::Printf(TEXT("Pair case %d timed out; saw=%d ended=%d interrupted=%d A=%s B=%s"), Mode, bSawPass, bEnded, bInterrupted,
				IsValid(A) ? *A->GetActorLocation().ToCompactString() : TEXT("destroyed"), IsValid(B) ? *B->GetActorLocation().ToCompactString() : TEXT("destroyed"))); return true;
		}
		return false;
	}
private:
	bool CheckChannels()
	{
		for (ARTSUnit* Unit : {A, B, Third})
		{
			if (!IsValid(Unit) || !Unit->IsAlive()) { continue; }
			const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent();
			if (Capsule->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block || Capsule->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block
				|| Capsule->GetMoveIgnoreActors().Contains(Third)) { Test->AddError(TEXT("Pair pass disabled global or third-party collision")); return false; }
		}
		return true;
	}
	bool CheckBoundaries(UWorld* World)
	{
		B->SetGenericTeamId(FGenericTeamId(1));
		Test->TestFalse(TEXT("Enemy cannot receive a collision exception"), FRTSTrafficPassTestAccess::Start(A, B));
		B->SetGenericTeamId(FGenericTeamId(0));
		Third->SetActorLocation(B->GetActorLocation() + FVector(0, Combined + 12, 0));
		Test->TestFalse(TEXT("Third unit blocks the exit"), FRTSTrafficPassTestAccess::Start(A, B));
		Third->SetActorLocation(FVector(500, 0, A->GetActorLocation().Z));
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(100, 10, 120)); Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetCanEverAffectNavigation(false); Box->RegisterComponent(); Wall->SetActorLocation(B->GetActorLocation() + FVector(0, Combined + 12, 0));
		Test->TestFalse(TEXT("Wall blocks the exit"), FRTSTrafficPassTestAccess::Start(A, B));
		Wall->SetActorLocation((A->GetActorLocation() + B->GetActorLocation()) * 0.5);
		Test->TestFalse(TEXT("Wall as first obstruction is never ignored"), FRTSTrafficPassTestAccess::Start(A, B));
		Test->TestEqual(TEXT("Rejected cases created no passes"), A->GetTrafficPassCount(), uint32(0));
		Test->TestTrue(TEXT("Rejected cases leave both lists empty"), A->GetCapsuleComponent()->GetMoveIgnoreActors().IsEmpty() && B->GetCapsuleComponent()->GetMoveIgnoreActors().IsEmpty());
		return true;
	}
	FAutomationTestBase* Test;
	int32 Mode, Stage = 0;
	ARTSUnit* A = nullptr; ARTSUnit* B = nullptr; ARTSUnit* Third = nullptr;
	float Combined = 0;
	bool bSawPass = false, bInterrupted = false, bEnded = false;
	double Started = FPlatformTime::Seconds(), SetupAt = 0, IssuedAt = 0, PassAt = 0, StableAt = -1;
};
}
bool FRTSTrafficPassTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckTrafficPass(this, FCString::Atoi(*Parameters)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
