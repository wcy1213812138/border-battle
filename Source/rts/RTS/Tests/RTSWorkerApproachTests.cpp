#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSOrderLibrary.h"
#include "Units/RTSUnit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSWorkerApproachTest, "MiniRTS.Economy.ContinuousWorkerDetour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	class FCheckWorkerDetour : public IAutomationLatentCommand
	{
	public:
		explicit FCheckWorkerDetour(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			if (!Worker)
			{
				FString Error;
				if (!RTSEconomyScenario::Create(World, Scenario, Error)) { Test->AddError(Error); return true; }
				Worker = Scenario.Workers[1];
				Start = Worker->GetActorLocation();
				Direction = (Scenario.Wood->GetActorLocation() - Start).GetSafeNormal2D();
				Side = FVector(-Direction.Y, Direction.X, 0);
				// A real stationary capsule blocks the direct route; no collision bypass.
				Blocker = URTSBalanceSubsystem::Get(World)->SpawnUnit(TEXT("Peasant"), 0,
					Start - FVector(0, 0, Worker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + Direction * 180);
				if (!Blocker) { Test->AddError(TEXT("Cannot spawn detour blocker")); return true; }
				Scenario.Wood->Extract(495);
				SetupAt = World->GetTimeSeconds();
				return false;
			}
			const double Now = World->GetTimeSeconds();
			if (!bIssued)
			{
				if (Now - SetupAt < 1) { return false; }
				URTSOrderLibrary::IssueWorkOrder({Worker}, Scenario.Wood);
				bIssued = true; IssuedAt = PreviousAt = Now;
				return false;
			}
			if (Now - IssuedAt > 30) { Test->AddError(TEXT("Worker detour/delivery timed out")); return true; }
			const double Delta = Now - PreviousAt; PreviousAt = Now;
			MinSeparation = FMath::Min(MinSeparation, FVector::Dist2D(Worker->GetActorLocation(), Blocker->GetActorLocation()));
			if (!bReachedResource)
			{
				MaxSideOffset = FMath::Max(MaxSideOffset, FMath::Abs(FVector::DotProduct(Worker->GetActorLocation() - Start, Side)));
				const AAIController* AI = Cast<AAIController>(Worker->GetController());
				if (AI && AI->GetCurrentMoveRequestID() != LastRequest)
				{ LastRequest = AI->GetCurrentMoveRequestID(); ++Requests; }
				if (Worker->GetGatherer()->IsWorking() || Worker->GetGatherer()->GetCarriedAmount() > 0)
				{
					bReachedResource = true;
					Test->TestTrue(TEXT("Actually detoured around stationary unit"), MaxSideOffset > 45);
					Test->TestTrue(TEXT("No timer-sized stop between route corners"), LongestPause < 0.5);
					Test->TestTrue(TEXT("Route requests bounded, not reissued every tick"), Requests <= 12);
					Test->AddInfo(FString::Printf(TEXT("Worker detour: longest travel pause %.3f s, requests %d, lateral detour %.1f cm."), LongestPause, Requests, MaxSideOffset));
				}
				else if (Now - IssuedAt > 0.2)
				{
					Pause = Worker->GetVelocity().Size2D() < 10 ? Pause + Delta : 0;
					LongestPause = FMath::Max(LongestPause, Pause);
				}
			}
			if (URTSBalanceSubsystem::Get(World)->GetResources(0)->GetAmount(ERTSResource::Wood) == 405)
			{
				Test->TestTrue(TEXT("Both capsules remained blocking without penetration"), MinSeparation >= 78
					&& Worker->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
				Test->TestEqual(TEXT("Final cargo delivered exactly"), Worker->GetGatherer()->GetCarriedAmount(), 0);
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		FRTSEconomyScenario Scenario;
		ARTSUnit* Worker = nullptr;
		ARTSUnit* Blocker = nullptr;
		FVector Start = FVector::ZeroVector, Direction = FVector::ZeroVector, Side = FVector::ZeroVector;
		FAIRequestID LastRequest;
		int32 Requests = 0;
		bool bIssued = false, bReachedResource = false;
		double Started = FPlatformTime::Seconds(), SetupAt = 0, IssuedAt = 0, PreviousAt = 0;
		double Pause = 0, LongestPause = 0, MaxSideOffset = 0, MinSeparation = TNumericLimits<double>::Max();
	};
}
bool FRTSWorkerApproachTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckWorkerDetour(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
