#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSCombatScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealthComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "UI/RTSResourceWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSStanceTest, "MiniRTS.Combat.Stances", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckStances : public IAutomationLatentCommand
	{
	public:
		explicit FCheckStances(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			const double Now = World->GetTimeSeconds();
			if (!A)
			{
				TArray<ARTSUnit*> Units; FString Error;
				if (!RTSCombatScenario::Create(World, 1, 0, Units, Error)) { Test->AddError(Error); return true; }
				const FVector P = Units[0]->GetActorLocation(), Q = Units[1]->GetActorLocation();
				URTSUnitData* Data = DuplicateObject<URTSUnitData>(Units[0]->GetUnitData(), World);
				Data->AttackRange = 800; Data->AcquisitionRadius = 400; Data->AttackDamage = 1;
				for (ARTSUnit* Unit : Units) { Unit->Destroy(); }
				auto Spawn = [&](const FVector& Location, uint8 Team)
				{
					const FTransform T(Location);
					ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					Unit->Configure(Data, Team); Unit->FinishSpawning(T); return Unit;
				};
				A = Spawn(P, 0); B = Spawn(Q, 1); Origin = P;
				A->SetCombatStance(ERTSCombatStance::Guard); B->SetCombatStance(ERTSCombatStance::Guard);
				At = Now; return false;
			}
			if (Now - At > 8) { Test->AddError(FString::Printf(TEXT("Stance stage %d timed out"), Stage)); return true; }
			if (Now - At < 1.1) { return false; }
			if (Stage == 0)
			{
				Test->TestEqual(TEXT("Guard does not proactively acquire"), A->GetCurrentOrder().Type, ERTSOrderType::None);
				A->GetHealth()->ApplyDamage(1, B);
			}
			else if (Stage == 1)
			{
				Test->TestTrue(TEXT("Guard retaliates against actual attacker"), A->GetCurrentOrder().Type == ERTSOrderType::Attack && A->GetCurrentOrder().TargetActor.Get() == B);
				Test->TestTrue(TEXT("Guard automatic position held"), A->IsHoldingAutomaticPosition());
				Test->TestTrue(TEXT("Guard did not chase"), FVector::Dist2D(Origin, A->GetActorLocation()) < 5);
				A->SetCombatStance(ERTSCombatStance::Patrol); URTSOrderLibrary::IssueStopOrder({B});
			}
			else if (Stage == 2)
			{
				Test->TestEqual(TEXT("Patrol respects smaller acquisition radius"), A->GetCurrentOrder().Type, ERTSOrderType::None);
				A->SetCombatStance(ERTSCombatStance::Aggressive);
			}
			else if (Stage == 3)
			{
				Test->TestTrue(TEXT("Aggressive detects enemy beyond patrol radius"), A->GetCurrentOrder().Type == ERTSOrderType::Attack);
				FRTSOrder Move; Move.Type = ERTSOrderType::Move; Move.TargetLocation = Origin + (Origin - B->GetActorLocation()).GetSafeNormal2D() * 150;
				A->ReceiveOrder(Move);
				A->SetCombatStance(ERTSCombatStance::Guard);
				// Exercise the same UFUNCTION used by the visible batch-policy button.
				ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
				const FVector Across = FVector::CrossProduct((B->GetActorLocation() - Origin).GetSafeNormal2D(), FVector::UpVector);
				const FTransform T(Origin + Across * 180);
				ARTSUnit* Ally = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ally->Configure(A->GetUnitData(), 0); Ally->FinishSpawning(T);
				PC->GetSelection()->SelectSingle(A, false); PC->GetSelection()->SelectSingle(Ally, true);
				URTSResourceWidget* Widget = CreateWidget<URTSResourceWidget>(PC, URTSResourceWidget::StaticClass());
				UFunction* ButtonFunction = Widget ? Widget->FindFunction(TEXT("SetAggressive")) : nullptr;
				Test->TestNotNull(TEXT("Batch stance button binding"), ButtonFunction);
				if (ButtonFunction) { Widget->ProcessEvent(ButtonFunction, nullptr); }
				Test->TestEqual(TEXT("Batch stance applied to first selected unit"), A->GetCombatStance(), ERTSCombatStance::Aggressive);
				Test->TestEqual(TEXT("Batch stance applied to second selected unit"), Ally->GetCombatStance(), ERTSCombatStance::Aggressive);
				Test->TestEqual(TEXT("Policy change preserves manual move"), A->GetCurrentOrder().Type, ERTSOrderType::Move);
				A->SetCombatStance(static_cast<ERTSCombatStance>(99));
				Test->TestEqual(TEXT("Invalid stance rejected"), A->GetCombatStance(), ERTSCombatStance::Aggressive);
				return true;
			}
			++Stage; At = Now; return false;
		}
	private:
		FAutomationTestBase* Test;
		ARTSUnit* A = nullptr;
		ARTSUnit* B = nullptr;
		FVector Origin = FVector::ZeroVector;
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds(), At = 0;
	};
}
bool FRTSStanceTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckStances(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
