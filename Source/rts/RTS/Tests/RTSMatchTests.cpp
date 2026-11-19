#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Match/RTSMatchSubsystem.h"
#include "AI/RTSEnemyDirector.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Units/RTSUnit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchLifecycleTest, "MiniRTS.Match.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMatchEconomyTest, "MiniRTS.Match.ComputerEconomyAndAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSDifficultyTest, "MiniRTS.Match.DifficultyParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	class FCheckMatch : public IAutomationLatentCommand
	{
	public:
		FCheckMatch(FAutomationTestBase* InTest, bool bInEconomy) : Test(InTest), bEconomy(bInEconomy) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World);
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
			if (!Match || !Balance) { Test->AddError(TEXT("Match systems unavailable")); return true; }
			FString Error;
			if (Step == 0)
			{
				if (!bEconomy && !bCheckedMenu)
				{
					const int32 Before = Match->GetUnitCount(0, false);
					Match->ToggleMainMenu();
					Test->TestTrue(TEXT("Sandbox menu opens and pauses"), Match->GetPhase() == ERTSMatchPhase::MainMenu && UGameplayStatics::IsGamePaused(World));
					Match->ToggleMainMenu();
					Test->TestTrue(TEXT("Second menu click restores sandbox"), Match->GetPhase() == ERTSMatchPhase::Sandbox && !UGameplayStatics::IsGamePaused(World));
					Test->TestEqual(TEXT("Menu toggle preserves sandbox units"), Match->GetUnitCount(0, false), Before);
					bCheckedMenu = true;
				}
				if (!bEconomy)
				{
					IConsoleManager::Get().ProcessUserConsoleInput(TEXT("rts.TestGameLoop"), *GLog, World);
					if (Match->GetPhase() != ERTSMatchPhase::Playing)
					{ if (FPlatformTime::Seconds() - Started < 35) { return false; } Test->AddError(TEXT("GameLoop command did not start a match")); return true; }
					Test->TestTrue(TEXT("GameLoop command defaults to easy"), Match->GetDifficulty() == ERTSDifficulty::Easy);
					ARTSBuilding* CommandBase = Match->GetMainBase(0);
					IConsoleManager::Get().ProcessUserConsoleInput(TEXT("rts.TestGameLoop 9"), *GLog, World);
					Test->TestTrue(TEXT("Invalid GameLoop argument preserves match"), Match->GetMainBase(0) == CommandBase && Match->GetDifficulty() == ERTSDifficulty::Easy);
					IConsoleManager::Get().ProcessUserConsoleInput(TEXT("rts.TestGameLoop 1"), *GLog, World);
					Test->TestTrue(TEXT("GameLoop command uses selected difficulty"), Match->GetDifficulty() == ERTSDifficulty::Normal);
				}
				else if (!Match->StartMatch(ERTSDifficulty::Normal, Error))
				{ if (FPlatformTime::Seconds() - Started < 35) { return false; } Test->AddError(Error); return true; }
				Test->TestTrue(TEXT("Both completed starting bases"), Match->GetMainBase(0) && Match->GetMainBase(1)
					&& Match->GetMainBase(0)->IsComplete() && Match->GetMainBase(1)->IsComplete());
				for (uint8 Team = 0; Team < 2; ++Team)
				{
					Test->TestEqual(TEXT("Equal starting workers"), Match->GetUnitCount(Team, true), 4);
					Test->TestEqual(TEXT("Equal starting army"), Match->GetUnitCount(Team, false), 2);
					Test->TestEqual(TEXT("Equal starting wood"), Balance->GetResources(Team)->GetAmount(ERTSResource::Wood), 300);
					Test->TestEqual(TEXT("Equal starting food"), Balance->GetResources(Team)->GetAmount(ERTSResource::Food), 250);
					Test->TestEqual(TEXT("Initial units excluded from training stats"), Match->GetStats(Team).UnitsTrained, 0);
				}
				OldDirector = Match->GetEnemyDirector(); OldHall = Match->GetMainBase(0);
				if (bEconomy) { Step = 10; return false; }
				const float BeforeMenuTime = Match->GetElapsedSeconds();
				Match->ToggleMainMenu();
				Test->TestTrue(TEXT("Playing menu opens and pauses"), Match->GetPhase() == ERTSMatchPhase::MainMenu && UGameplayStatics::IsGamePaused(World));
				Match->ToggleMainMenu();
				Test->TestTrue(TEXT("Closing menu resumes existing match"), Match->GetPhase() == ERTSMatchPhase::Playing && !UGameplayStatics::IsGamePaused(World));
				Test->TestTrue(TEXT("Menu preserves original base and director"), OldHall.Get() == Match->GetMainBase(0) && OldDirector.Get() == Match->GetEnemyDirector());
				Test->TestEqual(TEXT("Menu preserves match clock"), Match->GetElapsedSeconds(), BeforeMenuTime);
				ARTSUnit* Victim = nullptr; ARTSUnit* Killer = nullptr;
				for (TActorIterator<ARTSUnit> It(World); It; ++It)
				{ if (It->GetTeam() == 0) { Victim = *It; } else if (It->GetTeam() == 1) { Killer = *It; } }
				if (Victim && Killer) { Victim->GetHealth()->ApplyDamage(100000, Killer); }
				Test->TestEqual(TEXT("Actual death records loss"), Match->GetStats(0).UnitsLost, 1);
				Test->TestEqual(TEXT("Actual hostile killer receives credit"), Match->GetStats(1).UnitsKilled, 1);
				Match->SetMatchPaused(true); PausedAt = FPlatformTime::Seconds(); Elapsed = Match->GetElapsedSeconds();
				Test->TestTrue(TEXT("World genuinely paused"), UGameplayStatics::IsGamePaused(World));
				Test->TestFalse(TEXT("Paused production rejected"), Match->GetMainBase(0)->GetProducer()->Enqueue(TEXT("Peasant"), Error));
				Test->TestTrue(TEXT("Paused construction rejected"), Balance->PlaceBuilding(TEXT("Farm"), 0, FVector::ZeroVector, Error) == nullptr);
				Match->ToggleMainMenu(); Match->ToggleMainMenu();
				Test->TestTrue(TEXT("Closing menu restores previous pause"), Match->GetPhase() == ERTSMatchPhase::Paused && UGameplayStatics::IsGamePaused(World));
				Step = 1; return false;
			}
			if (Step == 1)
			{
				if (FPlatformTime::Seconds() - PausedAt < 0.5) { return false; }
				Test->TestEqual(TEXT("Pause freezes match clock"), Match->GetElapsedSeconds(), Elapsed);
				Test->TestEqual(TEXT("Pause freezes computer spending"), Balance->GetResources(1)->GetAmount(ERTSResource::Wood), 300);
				Match->SetMatchPaused(false);
				Match->GetMainBase(1)->GetHealth()->ApplyDamage(100000, Match->GetMainBase(0));
				Match->Tick(0);
				Test->TestTrue(TEXT("Enemy starting base death wins"), Match->GetPhase() == ERTSMatchPhase::Victory);
				Step = 2; return false;
			}
			if (Step >= 2 && Step <= 4)
			{
				if (!Match->StartMatch(static_cast<ERTSDifficulty>(Step - 2), Error)) { Test->AddError(Error); return true; }
				Test->TestFalse(TEXT("Old hall destroyed on restart"), OldHall.IsValid());
				Test->TestFalse(TEXT("Old director destroyed on restart"), OldDirector.IsValid());
				int32 Directors = 0; for (TActorIterator<ARTSEnemyDirector> It(World); It; ++It) { ++Directors; }
				Test->TestEqual(TEXT("Exactly one director after restart"), Directors, 1);
				Test->TestFalse(TEXT("Restart unpauses world"), UGameplayStatics::IsGamePaused(World));
				OldHall = Match->GetMainBase(0); OldDirector = Match->GetEnemyDirector();
				if (Step == 2)
				{
					Match->GetMainBase(0)->GetHealth()->ApplyDamage(100000, Match->GetMainBase(1)); Match->Tick(0);
					Test->TestTrue(TEXT("Player starting base death loses"), Match->GetPhase() == ERTSMatchPhase::Defeat);
				}
				if (Step == 3)
				{
					Match->GetMainBase(0)->GetHealth()->ApplyDamage(100000, nullptr);
					Match->GetMainBase(1)->GetHealth()->ApplyDamage(100000, nullptr); Match->Tick(0);
					Test->TestTrue(TEXT("Simultaneous starting base death draws"), Match->GetPhase() == ERTSMatchPhase::Draw);
				}
				if (Step == 4)
				{
					Match->SetMatchPaused(true); Match->Surrender();
					Test->TestTrue(TEXT("Surrender from pause loses"), Match->GetPhase() == ERTSMatchPhase::Defeat);
					Match->ShowMainMenu();
					Test->TestFalse(TEXT("Menu clears director"), Match->GetEnemyDirector() != nullptr);
					Test->TestEqual(TEXT("Menu clears units"), Match->GetUnitCount(0, true) + Match->GetUnitCount(1, true), 0);
					Test->TestFalse(TEXT("Menu unpauses world"), UGameplayStatics::IsGamePaused(World));
					Match->ToggleMainMenu();
					Test->TestTrue(TEXT("Startup or reset menu can close without return state"), Match->GetPhase() == ERTSMatchPhase::Sandbox && !UGameplayStatics::IsGamePaused(World));
					Match->EnterSandbox();
					Test->TestTrue(TEXT("Sandbox restores development gameplay"), Match->AllowsGameplay());
					return true;
				}
				++Step; return false;
			}
			if (Step == 10)
			{
				const FRTSMatchStats& Stats = Match->GetStats(1);
				bool bBarracks = false, bMilitaryTrained = Match->GetUnitCount(1, false) > 2;
				for (TActorIterator<ARTSBuilding> It(World); It; ++It)
				{ if (It->GetTeam() == 1 && It->GetBuildingId() == TEXT("Barracks") && It->IsComplete()) { bBarracks = true; } }
				bool bWave = false;
				for (TActorIterator<ARTSUnit> It(World); It; ++It)
				{
					if (It->GetTeam() == 1 && It->GetUnitData() && !It->GetUnitData()->bWorker
						&& It->GetCurrentOrder().Type == ERTSOrderType::Attack
						&& FVector::Dist2D(It->GetActorLocation(), Match->GetBaseLocation(1)) > 1500) { bWave = true; }
				}
				if (Stats.WoodGathered > 0 && Stats.FoodGathered > 0 && Stats.BuildingsBuilt > 0 && bBarracks && bMilitaryTrained && bWave)
				{
					Test->AddInfo(FString::Printf(TEXT("Computer at %.1fs: gathered %d wood/%d food, trained %d, built %d; military wave left base."), Match->GetElapsedSeconds(), Stats.WoodGathered, Stats.FoodGathered, Stats.UnitsTrained, Stats.BuildingsBuilt));
					Step = 11; return false;
				}
				if (Match->GetElapsedSeconds() > 230 || Match->GetPhase() != ERTSMatchPhase::Playing || FPlatformTime::Seconds() - Started > 280)
				{
					Test->AddError(FString::Printf(TEXT("Computer chain incomplete at %.1fs: wood=%d food=%d trained=%d built=%d barracks=%d army=%d wave=%d; %s"), Match->GetElapsedSeconds(), Stats.WoodGathered, Stats.FoodGathered, Stats.UnitsTrained, Stats.BuildingsBuilt, bBarracks, bMilitaryTrained, bWave, Match->GetEnemyDirector() ? *Match->GetEnemyDirector()->GetIntent() : TEXT("no director")));
					Match->ShowMainMenu(); return true;
				}
			}
			if (Step == 11)
			{
				if (Match->GetPhase() == ERTSMatchPhase::Defeat)
				{
					Test->TestTrue(TEXT("Real computer army destroyed starting base"), !Match->GetMainBase(0) || !Match->GetMainBase(0)->IsAlive());
					Test->AddInfo(FString::Printf(TEXT("Unassisted computer victory at %.1f seconds; match reached real defeat results."), Match->GetElapsedSeconds()));
					Match->ShowMainMenu(); return true;
				}
				if (Match->GetPhase() != ERTSMatchPhase::Playing || Match->GetElapsedSeconds() > 360 || FPlatformTime::Seconds() - Started > 420)
				{ Test->AddError(TEXT("Computer failed to finish an undefended match within 360 seconds.")); Match->ShowMainMenu(); return true; }
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		bool bEconomy;
		bool bCheckedMenu = false;
		int32 Step = 0;
		double Started = FPlatformTime::Seconds(), PausedAt = 0;
		float Elapsed = 0;
		TWeakObjectPtr<ARTSEnemyDirector> OldDirector;
		TWeakObjectPtr<ARTSBuilding> OldHall;
	};
}
bool FRTSMatchLifecycleTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckMatch(this, false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
bool FRTSMatchEconomyTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckMatch(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
bool FRTSDifficultyTest::RunTest(const FString&)
{
	FRTSAIDifficulty Easy, Normal, Hard; FString Error;
	TestTrue(TEXT("Easy profile valid"), ARTSEnemyDirector::LoadDifficulty(ERTSDifficulty::Easy, Easy, Error));
	TestTrue(TEXT("Normal profile valid"), ARTSEnemyDirector::LoadDifficulty(ERTSDifficulty::Normal, Normal, Error));
	TestTrue(TEXT("Hard profile valid"), ARTSEnemyDirector::LoadDifficulty(ERTSDifficulty::Hard, Hard, Error));
	TestTrue(TEXT("Decision interval decreases"), Easy.DecisionInterval > Normal.DecisionInterval && Normal.DecisionInterval > Hard.DecisionInterval);
	TestTrue(TEXT("Economic targets increase"), Easy.WorkerTarget < Normal.WorkerTarget && Normal.WorkerTarget < Hard.WorkerTarget);
	TestTrue(TEXT("First attacks get earlier"), Easy.FirstAttackSeconds > Normal.FirstAttackSeconds && Normal.FirstAttackSeconds > Hard.FirstAttackSeconds);
	TestFalse(TEXT("Invalid difficulty rejected"), ARTSEnemyDirector::LoadDifficulty(static_cast<ERTSDifficulty>(255), Normal, Error));
	return true;
}
#endif
