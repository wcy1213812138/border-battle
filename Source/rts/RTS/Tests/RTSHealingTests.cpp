#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSDuelScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealFeedbackComponent.h"
#include "Components/StaticMeshComponent.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSHealingTest, "MiniRTS.Combat.Healing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
class FCheckHealing : public IAutomationLatentCommand
{
public:
	FCheckHealing(FAutomationTestBase* InTest, FString InKind) : Test(InTest), Kind(MoveTemp(InKind)) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || !World->HasBegunPlay())
		{ if (FPlatformTime::Seconds() - Start < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
		if (World->GetTimeSeconds() < 2) { return false; }
		if (Stage == 0)
		{
			TArray<ARTSUnit*> Units; FString Error;
			if (!RTSDuelScenario::Create(World, FName(*Kind), TEXT("Swordman"), 1, Units, Error))
			{ if (FPlatformTime::Seconds() - Start < 30) { return false; } Test->AddError(Error); return true; }
			if (Units.Num() != 3) { Test->AddError(TEXT("Expected healer, enemy and ally")); return true; }
			Healer = Units[0]; Enemy = Units[1]; Ally = Units[2];
			FRTSOrder Stop; Stop.Type = ERTSOrderType::Stop;
			Enemy->ReceiveOrder(Stop); Ally->ReceiveOrder(Stop);
			Enemy->GetHealth()->ApplyDamage(30, nullptr);
			AllyBefore = Ally->GetHealth()->GetHealth(); EnemyBefore = Enemy->GetHealth()->GetHealth();
			Test->TestTrue(TEXT("Healer rejects enemy attack order"), Healer->GetCurrentOrder().Type == ERTSOrderType::None);
			PhaseTime = World->GetTimeSeconds(); Stage = 1; return false;
		}
		if (!Healer.IsValid() || !Ally.IsValid() || !Enemy.IsValid()) { Test->AddError(TEXT("Unexpected destroyed unit")); return true; }
		if (World->GetTimeSeconds() - PhaseTime < 3) { return false; }
		if (Stage == 1)
		{
			Test->TestTrue(TEXT("Wounded ally actually healed"), Ally->GetHealth()->GetHealth() > AllyBefore);
			for (ARTSUnit* Unit : {Healer.Get(), Ally.Get()})
			{
				URTSHealFeedbackComponent* FX = Unit->FindComponentByClass<URTSHealFeedbackComponent>();
				Test->TestTrue(TEXT("Actual healing triggers feedback"), FX && FX->GetPulseCount() > 0);
				if (!FX) { continue; }
				FX->Pulse(); FX->Pulse();
				TArray<UStaticMeshComponent*> Meshes; Unit->GetComponents(Meshes);
				int32 Count = 0;
				for (UStaticMeshComponent* Mesh : Meshes)
				{
					if (!Mesh->GetName().StartsWith(TEXT("HealFX_"))) { continue; }
					++Count;
					Test->TestTrue(TEXT("Healing FX has no collision or navigation"), Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Mesh->CanEverAffectNavigation());
				}
				Test->TestEqual(TEXT("Repeated pulses reuse twelve parts"), Count, 12);
			}
			Test->TestEqual(TEXT("Enemy not healed or damaged by healer"), Enemy->GetHealth()->GetHealth(), EnemyBefore);
			FRTSOrder Stop; Stop.Type = ERTSOrderType::Stop; Healer->ReceiveOrder(Stop);
			Ally->GetHealth()->ApplyDamage(35, nullptr); AllyBefore = Ally->GetHealth()->GetHealth();
			PhaseTime = World->GetTimeSeconds(); Stage = 2; return false;
		}
		Test->TestEqual(TEXT("Stop suppresses automatic healing"), Ally->GetHealth()->GetHealth(), AllyBefore);
		for (ARTSUnit* Unit : {Healer.Get(), Ally.Get()})
		{
			URTSHealFeedbackComponent* FX = Unit->FindComponentByClass<URTSHealFeedbackComponent>();
			Test->TestTrue(TEXT("Finished feedback stops ticking"), FX && !FX->IsPlaying() && !FX->IsComponentTickEnabled());
			TArray<UStaticMeshComponent*> Meshes; Unit->GetComponents(Meshes);
			for (UStaticMeshComponent* Mesh : Meshes)
			{ if (Mesh->GetName().StartsWith(TEXT("HealFX_"))) { Test->TestFalse(TEXT("Finished FX hidden"), Mesh->IsVisible()); } }
		}
		Ally->GetHealth()->ApplyHealing(10000);
		Test->TestEqual(TEXT("Healing cannot exceed max health"), Ally->GetHealth()->GetHealth(), Ally->GetHealth()->GetMaxHealth());
		Ally->GetHealth()->ApplyDamage(10000, nullptr);
		Test->TestEqual(TEXT("Healing cannot resurrect"), Ally->GetHealth()->ApplyHealing(100), 0.f);
		return true;
	}
private:
	FAutomationTestBase* Test;
	FString Kind;
	TWeakObjectPtr<ARTSUnit> Healer, Ally, Enemy;
	int32 Stage = 0;
	double Start = FPlatformTime::Seconds(), PhaseTime = 0;
	float AllyBefore = 0, EnemyBefore = 0;
};
}
void FRTSHealingTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{ Names = {TEXT("Priest"), TEXT("HighPriest")}; Commands = Names; }
bool FRTSHealingTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckHealing(this, Parameters));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
