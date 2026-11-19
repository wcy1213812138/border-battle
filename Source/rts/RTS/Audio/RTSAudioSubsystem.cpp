#include "Audio/RTSAudioSubsystem.h"
#include "Match/RTSMatchSubsystem.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* CueNames[] = { TEXT("S_Select"), TEXT("S_Order"), TEXT("S_Swing"), TEXT("S_Bow"), TEXT("S_Hit"), TEXT("S_Death"), TEXT("S_Victory"), TEXT("S_Defeat") };
	constexpr double Cooldowns[] = { 0.12, 0.12, 0.09, 0.12, 0.09, 0.18, 1.0, 1.0 };
	USoundWave* LoadWave(const TCHAR* Name)
	{
		return LoadObject<USoundWave>(nullptr, *FString::Printf(TEXT("/Game/RTS/Audio/%s.%s"), Name, Name));
	}
}
URTSAudioSubsystem* URTSAudioSubsystem::Get(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URTSAudioSubsystem>() : nullptr;
}
bool URTSAudioSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void URTSAudioSubsystem::LoadAssets()
{
	Cues.Reset();
	for (const TCHAR* Name : CueNames) { Cues.Add(LoadWave(Name)); }
	MenuMusic = LoadWave(TEXT("M_Menu")); BattleMusic = LoadWave(TEXT("M_Battle"));
	LastCueTimes.Init(-1.e9, static_cast<int32>(ERTSAudioCue::Count));
	AcceptedCounts.Init(0, static_cast<int32>(ERTSAudioCue::Count));
}
void URTSAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_DedicatedServer) { return; }
 if(GConfig)
 {
  float Value=1.f;
  if(GConfig->GetFloat(TEXT("MiniRTS.Audio"),TEXT("Master"),Value,GGameUserSettingsIni))SetVolume(Value);
  if(GConfig->GetFloat(TEXT("MiniRTS.Audio"),TEXT("Music"),Value,GGameUserSettingsIni))SetMusicVolume(Value);
  if(GConfig->GetFloat(TEXT("MiniRTS.Audio"),TEXT("Effects"),Value,GGameUserSettingsIni))SetEffectsVolume(Value);
 }
	LoadAssets();
	BoundMatch = URTSMatchSubsystem::Get(this);
	if (BoundMatch.IsValid()) { BoundMatch->OnMatchChanged.AddUObject(this, &URTSAudioSubsystem::SyncMatch); }
	SyncMatch();
}
USoundWave* URTSAudioSubsystem::GetCueAsset(ERTSAudioCue Cue) const
{
	const int32 Index = static_cast<int32>(Cue);
	return Cues.IsValidIndex(Index) ? Cues[Index].Get() : nullptr;
}
USoundWave* URTSAudioSubsystem::GetMusicAsset(ERTSMusic Value) const
{ return Value == ERTSMusic::Menu ? MenuMusic.Get() : Value == ERTSMusic::Battle ? BattleMusic.Get() : nullptr; }
int32 URTSAudioSubsystem::GetAcceptedCueCount(ERTSAudioCue Cue) const
{
	const int32 Index = static_cast<int32>(Cue);
	return AcceptedCounts.IsValidIndex(Index) ? AcceptedCounts[Index] : 0;
}
bool URTSAudioSubsystem::PlayCue(ERTSAudioCue Cue, const AActor* Source)
{
	const int32 Index = static_cast<int32>(Cue);
	USoundWave* Wave = GetCueAsset(Cue);
	UWorld* World = GetWorld();
	if (!World || !Wave || !LastCueTimes.IsValidIndex(Index) || Volume * EffectsVolume <= 0.f) { return false; }
	const bool bResult = Cue == ERTSAudioCue::Victory || Cue == ERTSAudioCue::Defeat;
	const URTSMatchSubsystem* Match = BoundMatch.Get();
	if (!bResult && ((Match && !Match->AllowsGameplay()) || UGameplayStatics::IsGamePaused(World))) { return false; }
	// Combat audio is local to what the player can currently see, not omniscient map-wide noise.
	if (Cue >= ERTSAudioCue::Swing && Cue <= ERTSAudioCue::Death)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (!IsValid(Source) || !PC || !PC->PlayerCameraManager) { return false; }
		const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
		const FVector Local = View.Rotation.UnrotateVector(Source->GetActorLocation() - View.Location);
		const float Aspect = FMath::Max(0.1f, View.AspectRatio);
		const bool bOrtho = View.ProjectionMode == ECameraProjectionMode::Orthographic;
		const float HalfWidth = bOrtho ? View.OrthoWidth * 0.5f : Local.X * FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5f));
		if (Local.X <= 0 || FMath::Abs(Local.Y) > HalfWidth || FMath::Abs(Local.Z) > HalfWidth / Aspect) { return false; }
	}
	const double Now = World->GetRealTimeSeconds();
	if (Now - LastCueTimes[Index] < Cooldowns[Index]) { return false; }
	ActiveCues.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& Item) { return !Item.IsValid() || !Item->IsPlaying(); });
	if (ActiveCues.Num() >= 12) { return false; }
	LastCueTimes[Index] = Now; ++AcceptedCounts[Index];
	// The request policy is also exercised under -nosound; no device is a harmless no-op.
	if (UAudioComponent* Component = UGameplayStatics::CreateSound2D(this, Wave, Volume * EffectsVolume * 0.55f, 1.f, 0.f, nullptr, false, true))
	{
		Component->bIsUISound = bResult || Cue == ERTSAudioCue::Select || Cue == ERTSAudioCue::Order;
		Component->bShouldRemainActiveIfDropped = false;
		ActiveCues.Add(Component); Component->Play();
	}
	return true;
}
void URTSAudioSubsystem::SetVolume(float Value)
{
	if (!FMath::IsFinite(Value)) { return; }
	Volume = FMath::Clamp(Value, 0.f, 1.f);
 ApplyVolumes();
}
void URTSAudioSubsystem::SetMusicVolume(float Value)
{
 if(!FMath::IsFinite(Value))return;
 MusicVolume=FMath::Clamp(Value,0.f,1.f);ApplyVolumes();
}
void URTSAudioSubsystem::SetEffectsVolume(float Value)
{
 if(!FMath::IsFinite(Value))return;
 EffectsVolume=FMath::Clamp(Value,0.f,1.f);ApplyVolumes();
}
void URTSAudioSubsystem::ApplyVolumes()
{
 if(MusicComponent)MusicComponent->SetVolumeMultiplier(Volume * MusicVolume * .23f);
 for(const auto& Item:ActiveCues)if(Item.IsValid())Item->SetVolumeMultiplier(Volume * EffectsVolume * .55f);
}
void URTSAudioSubsystem::SaveVolumeSettings() const
{
 if(!GConfig)return;
 GConfig->SetFloat(TEXT("MiniRTS.Audio"),TEXT("Master"),Volume,GGameUserSettingsIni);
 GConfig->SetFloat(TEXT("MiniRTS.Audio"),TEXT("Music"),MusicVolume,GGameUserSettingsIni);
 GConfig->SetFloat(TEXT("MiniRTS.Audio"),TEXT("Effects"),EffectsVolume,GGameUserSettingsIni);
 GConfig->Flush(false,GGameUserSettingsIni);
}
void URTSAudioSubsystem::SetMusic(ERTSMusic Value, bool bPaused)
{
	if (MusicState != Value)
	{
		if (MusicComponent) { MusicComponent->Stop(); MusicComponent->DestroyComponent(); MusicComponent = nullptr; }
		MusicState = Value;
	}
	bMusicPaused = bPaused;
	if (!MusicComponent && GetMusicAsset(Value))
	{
		MusicComponent = UGameplayStatics::CreateSound2D(this, GetMusicAsset(Value), Volume * MusicVolume * 0.23f, 1.f, 0.f, nullptr, false, false);
		if (MusicComponent)
		{
			MusicComponent->bIsUISound = true;
			MusicComponent->Play();
		}
	}
	if (MusicComponent) { MusicComponent->SetPaused(bPaused); }
}
void URTSAudioSubsystem::SyncMatch()
{
	const URTSMatchSubsystem* Match = BoundMatch.Get();
	if (!Match) { return; }
	const ERTSMatchPhase Phase = Match->GetPhase();
	const int32 PhaseValue = static_cast<int32>(Phase);
	const bool bChanged = LastPhase != PhaseValue;
	LastPhase = PhaseValue;
	if (Phase == ERTSMatchPhase::MainMenu) { SetMusic(ERTSMusic::Menu, false); }
	else if (Phase == ERTSMatchPhase::Playing || Phase == ERTSMatchPhase::Paused)
	{ SetMusic(ERTSMusic::Battle, Phase == ERTSMatchPhase::Paused); }
	else { SetMusic(ERTSMusic::None, false); }
	if (bChanged && !Match->AllowsGameplay())
	{
		for (const TWeakObjectPtr<UAudioComponent>& Item : ActiveCues) { if (Item.IsValid()) { Item->Stop(); } }
		ActiveCues.Reset();
	}
	if (Match->AllowsGameplay()) { bResultPlayed = false; }
	if (bChanged && !bResultPlayed && Phase == ERTSMatchPhase::Victory)
	{ bResultPlayed = true; PlayCue(ERTSAudioCue::Victory); }
	else if (bChanged && !bResultPlayed && (Phase == ERTSMatchPhase::Defeat || Phase == ERTSMatchPhase::Draw))
	{ bResultPlayed = true; PlayCue(ERTSAudioCue::Defeat); }
}
void URTSAudioSubsystem::StopAudio()
{
	if (MusicComponent) { MusicComponent->Stop(); MusicComponent->DestroyComponent(); MusicComponent = nullptr; }
	for (const TWeakObjectPtr<UAudioComponent>& Item : ActiveCues) { if (Item.IsValid()) { Item->Stop(); } }
	ActiveCues.Reset(); MusicState = ERTSMusic::None; bMusicPaused = false;
}
void URTSAudioSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	if (BoundMatch.IsValid()) { BoundMatch->OnMatchChanged.RemoveAll(this); }
	StopAudio(); BoundMatch.Reset();
	Super::OnWorldEndPlay(InWorld);
}
void URTSAudioSubsystem::Deinitialize()
{
	if (BoundMatch.IsValid()) { BoundMatch->OnMatchChanged.RemoveAll(this); }
	StopAudio(); BoundMatch.Reset(); Cues.Reset(); MenuMusic = nullptr; BattleMusic = nullptr;
	Super::Deinitialize();
}
