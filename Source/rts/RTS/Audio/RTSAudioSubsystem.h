#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSAudioSubsystem.generated.h"

class USoundWave;
class UAudioComponent;
class URTSMatchSubsystem;

UENUM()
enum class ERTSAudioCue : uint8
{
	Select, Order, Swing, Bow, Hit, Death, Victory, Defeat, Count
};
UENUM()
enum class ERTSMusic : uint8 { None, Menu, Battle };

// Presentation only. Audio failure or muting never changes gameplay.
UCLASS()
class URTSAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	static URTSAudioSubsystem* Get(const UObject* Context);
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	bool PlayCue(ERTSAudioCue Cue, const AActor* Source = nullptr);
	void SetVolume(float Value);
	float GetVolume() const { return Volume; }
 void SetMusicVolume(float Value);
 void SetEffectsVolume(float Value);
 float GetMusicVolume() const { return MusicVolume; }
 float GetEffectsVolume() const { return EffectsVolume; }
 void SaveVolumeSettings() const;
	USoundWave* GetCueAsset(ERTSAudioCue Cue) const;
	USoundWave* GetMusicAsset(ERTSMusic Value) const;
	ERTSMusic GetMusic() const { return MusicState; }
	bool IsMusicPaused() const { return bMusicPaused; }
	int32 GetAcceptedCueCount(ERTSAudioCue Cue) const;
	void SyncMatch();
private:
	void LoadAssets();
 void ApplyVolumes();
	void SetMusic(ERTSMusic Value, bool bPaused);
	void StopAudio();
	UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> Cues;
	UPROPERTY(Transient) TObjectPtr<USoundWave> MenuMusic;
	UPROPERTY(Transient) TObjectPtr<USoundWave> BattleMusic;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
	TArray<TWeakObjectPtr<UAudioComponent>> ActiveCues;
	TWeakObjectPtr<URTSMatchSubsystem> BoundMatch;
	TArray<double> LastCueTimes;
	TArray<int32> AcceptedCounts;
	ERTSMusic MusicState = ERTSMusic::None;
	int32 LastPhase = INDEX_NONE;
	float Volume = 1.f;
 float MusicVolume = 1.f;
 float EffectsVolume = 1.f;
	bool bMusicPaused = false;
	bool bResultPlayed = false;
};
