#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "Audio/RTSAudioSubsystem.h"
#include "Audio/RTSAudioFeedbackComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Units/RTSUnit.h"
#include "Match/RTSMatchSubsystem.h"
#include "UI/RTSMatchWidget.h"
#include "Sound/SoundWave.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAudioTest, "MiniRTS.Audio.AssetsEventsAndLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckAudio : public IAutomationLatentCommand
	{
	public:
		explicit FCheckAudio(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(World);
			URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World);
			if (!Audio || !Match) { Test->AddError(TEXT("Audio/match subsystem unavailable")); return true; }
			FString Error;
			if (Stage == 0)
			{
				for (int32 i = 0; i < static_cast<int32>(ERTSAudioCue::Count); ++i)
				{
					USoundWave* Wave = Audio->GetCueAsset(static_cast<ERTSAudioCue>(i));
					Test->TestNotNull(TEXT("Cue asset loads"), Wave);
					if (Wave) { Test->TestTrue(TEXT("One-shot has positive duration and does not loop"), Wave->Duration > 0 && !Wave->bLooping); }
				}
				for (ERTSMusic Music : {ERTSMusic::Menu, ERTSMusic::Battle})
				{
					USoundWave* Wave = Audio->GetMusicAsset(Music);
					Test->TestNotNull(TEXT("Music asset loads"), Wave);
					if (Wave) { Test->TestTrue(TEXT("Music loops and lasts 16 seconds"), Wave->bLooping && FMath::IsNearlyEqual(Wave->Duration, 16.f, 0.01f)); }
				}
				Test->TestTrue(TEXT("Sandbox has no automatic BGM"), Audio->GetMusic() == ERTSMusic::None);
				Audio->SetVolume(-3); Test->TestEqual(TEXT("Volume clamps to zero"), Audio->GetVolume(), 0.f);
				Test->TestFalse(TEXT("Muted cue rejected"), Audio->PlayCue(ERTSAudioCue::Select));
				Audio->SetVolume(3); Test->TestEqual(TEXT("Volume clamps to one"), Audio->GetVolume(), 1.f);
				Test->TestFalse(TEXT("Invalid cue rejected"), Audio->PlayCue(static_cast<ERTSAudioCue>(255)));
				Test->TestTrue(TEXT("First select accepted"), Audio->PlayCue(ERTSAudioCue::Select));
				Test->TestFalse(TEXT("Repeated selection throttled"), Audio->PlayCue(ERTSAudioCue::Select));
				Test->TestTrue(TEXT("Order category independent"), Audio->PlayCue(ERTSAudioCue::Order));
				if (!RTSEconomyScenario::Create(World, Scenario, Error)) { Test->AddError(Error); return true; }
				Unit = Scenario.Workers[0];
				Test->TestNotNull(TEXT("Unit audio listener installed"), Unit->FindComponentByClass<URTSAudioFeedbackComponent>());
				Test->TestNotNull(TEXT("Building audio listener installed"), Scenario.Hall->FindComponentByClass<URTSAudioFeedbackComponent>());
				Camera = World->SpawnActor<ACameraActor>(Unit->GetActorLocation() + FVector(0, 0, 1600), FRotator(-90, 0, 0));
				World->GetFirstPlayerController()->SetViewTarget(Camera);
				At = World->GetTimeSeconds(); ++Stage; return false;
			}
			if (Stage == 1)
			{
				if (World->GetTimeSeconds() - At < 0.3) { return false; }
				const int32 Hits = Audio->GetAcceptedCueCount(ERTSAudioCue::Hit);
				Unit->GetHealth()->ResetHealth();
				Test->TestEqual(TEXT("Healing remains silent"), Audio->GetAcceptedCueCount(ERTSAudioCue::Hit), Hits);
				const FVector VisibleLocation = Unit->GetActorLocation();
				Unit->SetActorLocation(Camera->GetActorLocation() + FVector(0, 0, 500));
				Test->TestFalse(TEXT("Behind-camera combat audio rejected"), Audio->PlayCue(ERTSAudioCue::Hit, Unit));
				Unit->SetActorLocation(VisibleLocation);
				Unit->GetHealth()->ApplyDamage(1, Scenario.Hall);
				Test->TestEqual(TEXT("Visible real damage requests hit audio"), Audio->GetAcceptedCueCount(ERTSAudioCue::Hit), Hits + 1);
				const int32 Swings = Audio->GetAcceptedCueCount(ERTSAudioCue::Swing);
				Unit->GetAttack()->OnAttackStarted.Broadcast(Scenario.Hall);
				Test->TestEqual(TEXT("Attack event reaches audio listener"), Audio->GetAcceptedCueCount(ERTSAudioCue::Swing), Swings + 1);
				const int32 Bows = Audio->GetAcceptedCueCount(ERTSAudioCue::Bow);
				Unit->GetAttack()->SetProjectileStats(true, 1200.f);
				Unit->GetAttack()->OnAttackStarted.Broadcast(Scenario.Hall);
				Test->TestEqual(TEXT("Ranged attack chooses bow cue"), Audio->GetAcceptedCueCount(ERTSAudioCue::Bow), Bows + 1);
				const int32 Deaths = Audio->GetAcceptedCueCount(ERTSAudioCue::Death);
				Unit->GetHealth()->ApplyDamage(10000, Scenario.Hall);
				Test->TestTrue(TEXT("Audio leaves actual lethal damage intact"), Unit->GetHealth()->IsDead());
				Test->TestEqual(TEXT("Visible death requests audio"), Audio->GetAcceptedCueCount(ERTSAudioCue::Death), Deaths + 1);
				Test->TestEqual(TEXT("Lethal hit does not double with hit cue"), Audio->GetAcceptedCueCount(ERTSAudioCue::Hit), Hits + 1);
				URTSAudioFeedbackComponent* Listener = Unit->FindComponentByClass<URTSAudioFeedbackComponent>();
				Listener->DestroyComponent();
				Test->TestFalse(TEXT("Destroyed listener unbinds attack"), Unit->GetAttack()->OnAttackStarted.Contains(Listener, TEXT("AttackStarted")));
				Audio->SetVolume(0.4f);
				Match->ToggleMainMenu();
				Test->TestTrue(TEXT("Menu chooses menu music"), Audio->GetMusic() == ERTSMusic::Menu && !Audio->IsMusicPaused());
				Test->TestFalse(TEXT("Menu blocks gameplay cues"), Audio->PlayCue(ERTSAudioCue::Order));
				Match->ToggleMainMenu();
				Test->TestTrue(TEXT("Close menu returns silent sandbox"), Audio->GetMusic() == ERTSMusic::None);
				++Stage;
			}
			if (Stage == 2)
			{
				if (!Match->StartMatch(ERTSDifficulty::Normal, Error))
				{ if (FPlatformTime::Seconds() - Started < 35) { return false; } Test->AddError(Error); return true; }
				Test->TestTrue(TEXT("Playing selects battle music"), Audio->GetMusic() == ERTSMusic::Battle && !Audio->IsMusicPaused());
				Match->SetMatchPaused(true);
				Test->TestTrue(TEXT("Pause retains track and pauses"), Audio->GetMusic() == ERTSMusic::Battle && Audio->IsMusicPaused());
				Test->TestFalse(TEXT("Pause blocks gameplay cue"), Audio->PlayCue(ERTSAudioCue::Order));
				Match->SetMatchPaused(false);
				Test->TestFalse(TEXT("Resume unpauses music"), Audio->IsMusicPaused());
				const int32 Results = Audio->GetAcceptedCueCount(ERTSAudioCue::Defeat);
				Match->Surrender();
				Test->TestTrue(TEXT("Results stop battle music"), Audio->GetMusic() == ERTSMusic::None);
				Test->TestEqual(TEXT("Result cue once"), Audio->GetAcceptedCueCount(ERTSAudioCue::Defeat), Results + 1);
				Match->OnMatchChanged.Broadcast();
				Match->ToggleMainMenu(); Match->ToggleMainMenu();
				Test->TestEqual(TEXT("Refresh/menu do not replay result"), Audio->GetAcceptedCueCount(ERTSAudioCue::Defeat), Results + 1);
				Match->ShowMainMenu();
				Test->TestTrue(TEXT("Reset returns menu music"), Audio->GetMusic() == ERTSMusic::Menu);
				Test->TestEqual(TEXT("Match resets preserve chosen volume"), Audio->GetVolume(), 0.4f);
				URTSMatchWidget* Widget = CreateWidget<URTSMatchWidget>(World->GetFirstPlayerController(), URTSMatchWidget::StaticClass());
				UFunction* VolumeFunction = Widget->FindFunction(TEXT("VolumeChanged"));
				Test->TestNotNull(TEXT("Volume slider callback exists"), VolumeFunction);
				if (VolumeFunction) { struct { float Value; } Params{0.f}; Widget->ProcessEvent(VolumeFunction, &Params); }
				Test->TestEqual(TEXT("Slider callback controls project volume"), Audio->GetVolume(), 0.f);
				Audio->SetVolume(1.f); ++Stage; return false;
			}
			if (Stage == 3)
			{
				if (!Match->StartMatch(ERTSDifficulty::Normal, Error)) { Test->AddError(Error); return true; }
				const int32 Wins = Audio->GetAcceptedCueCount(ERTSAudioCue::Victory);
				Match->GetMainBase(1)->GetHealth()->ApplyDamage(10000, Match->GetMainBase(0));
				ExpectedWins = Wins + 1; ++Stage; return false;
			}
			if (Stage == 4)
			{
				if (Match->GetPhase() != ERTSMatchPhase::Victory)
				{ if (FPlatformTime::Seconds() - Started < 40) { return false; } Test->AddError(TEXT("Victory phase unavailable")); return true; }
				Test->TestEqual(TEXT("Victory cue once after actual base death"), Audio->GetAcceptedCueCount(ERTSAudioCue::Victory), ExpectedWins);
				Test->TestTrue(TEXT("Victory stops BGM"), Audio->GetMusic() == ERTSMusic::None);
				Audio->OnWorldEndPlay(*World);
				Test->TestTrue(TEXT("World end clears music"), Audio->GetMusic() == ERTSMusic::None);
				Camera->Destroy(); return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		FRTSEconomyScenario Scenario;
		ARTSUnit* Unit = nullptr;
		ACameraActor* Camera = nullptr;
		int32 Stage = 0;
		int32 ExpectedWins = 0;
		double Started = FPlatformTime::Seconds(), At = 0;
	};
}
bool FRTSAudioTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckAudio(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
