#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSDuelScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSUnitData.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSDuelTest, "MiniRTS.Combat.InfantryDuel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
class FCheckDuel : public IAutomationLatentCommand
{
public:
	FCheckDuel(FAutomationTestBase* InTest, FString InPair) : Test(InTest) { InPair.Split(TEXT("/"), &Left, &Right); }
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || !World->HasBegunPlay())
		{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
		if (World->GetTimeSeconds() < 2) { return false; }
		if (Begin < 0)
		{
			FString Error; TArray<ARTSUnit*> Spawned;
			if (!RTSDuelScenario::Create(World, FName(*Left), FName(*Right), 3, Spawned, Error))
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(Error); return true; }
			Expected = 2 + (Spawned.Num() >= 2 ? int32(Spawned[0]->GetUnitData()->bHealer) + int32(Spawned[1]->GetUnitData()->bHealer) : 0);
			Test->TestEqual(TEXT("Duelists plus healer escorts"), Spawned.Num(), Expected);
			if (Spawned.Num() != Expected) { return true; }
			int32 EscortIndex = 2;
			A = Spawned[0]->GetUnitData()->bHealer ? Spawned[EscortIndex++] : Spawned[0];
			B = Spawned[1]->GetUnitData()->bHealer ? Spawned[EscortIndex++] : Spawned[1];
			Begin = World->GetTimeSeconds();
			InitialA = A->GetHealth()->GetHealth(); InitialB = B->GetHealth()->GetHealth();
			Test->TestTrue(TEXT("Mutual explicit fighter attack targets"), A->GetCurrentOrder().TargetActor == B && B->GetCurrentOrder().TargetActor == A);
			Test->TestTrue(TEXT("No automatic hostile acquisition"), !A->GetUnitData()->bAutoAcquireTargets && !B->GetUnitData()->bAutoAcquireTargets);
			return false;
		}
		if (World->GetTimeSeconds() - Begin < 15) { return false; }
		const bool HurtA = !A.IsValid() || !A->IsAlive() || A->GetHealth()->GetHealth() < InitialA;
		const bool HurtB = !B.IsValid() || !B->IsAlive() || B->GetHealth()->GetHealth() < InitialB;
		Test->TestTrue(TEXT("Duel causes real damage"), HurtA || HurtB);
		Test->TestTrue(TEXT("PIE remains active"), GEditor->PlayWorld == World);
		TArray<ARTSUnit*> Replaced; FString Error;
		Test->TestTrue(TEXT("Can restart duel"), RTSDuelScenario::Create(World, FName(*Left), FName(*Right), 1, Replaced, Error));
		int32 Count = 0; for (TActorIterator<ARTSUnit> It(World); It; ++It) { ++Count; }
		Test->TestEqual(TEXT("Restart leaves expected fighters and support"), Count, Expected);
		return true;
	}
private:
	FAutomationTestBase* Test;
	FString Left, Right;
	TWeakObjectPtr<ARTSUnit> A, B;
	double Started = FPlatformTime::Seconds();
	double Begin = -1;
	int32 Expected = 2;
	float InitialA = 0, InitialB = 0;
};
}
void FRTSDuelTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const FString Pair : {TEXT("Swordman/Spearman"), TEXT("Archer/Crossbowman"), TEXT("Light_Infantry/Heavy_Infantry"), TEXT("HeavySwordman/Halberdier"), TEXT("Mage/Priest"), TEXT("Paladin/HighPriest"), TEXT("Commander/King"), TEXT("Peasant/Scout")})
	{ Names.Add(Pair.Replace(TEXT("/"), TEXT("Vs"))); Commands.Add(Pair); }
}
bool FRTSDuelTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckDuel(this, Parameters));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
