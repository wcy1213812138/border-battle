#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSMatchSubsystem.generated.h"

class ARTSBuilding;
class ARTSUnit;
class URTSHealthComponent;
class URTSResourceComponent;
class ARTSEnemyDirector;

UENUM()
enum class ERTSMatchPhase : uint8
{
	Sandbox, MainMenu, Playing, Paused, Victory, Defeat, Draw
};
UENUM()
enum class ERTSDifficulty : uint8
{
	Easy, Normal, Hard
};

struct FRTSMatchStats
{
	int32 UnitsTrained = 0;
	int32 UnitsLost = 0;
	int32 UnitsKilled = 0;
	int32 BuildingsBuilt = 0;
	int32 WoodGathered = 0;
	int32 FoodGathered = 0;
};

// Match orchestration exposes state/events only. Gameplay has no dependency on widgets.
UCLASS()
class URTSMatchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static URTSMatchSubsystem* Get(const UObject* Context);
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual void Deinitialize() override;
	ERTSMatchPhase GetPhase() const { return Phase; }
	ERTSDifficulty GetDifficulty() const { return Difficulty; }
	static FText DifficultyName(ERTSDifficulty Value);
	bool AllowsGameplay() const { return Phase == ERTSMatchPhase::Sandbox || Phase == ERTSMatchPhase::Playing; }
	bool IsManagedMatch() const { return Phase != ERTSMatchPhase::Sandbox && Phase != ERTSMatchPhase::MainMenu; }
	bool StartMatch(ERTSDifficulty Value, FString& Error);
	void ShowMainMenu();
	void ToggleMainMenu();
	void EnterSandbox();
	void SetMatchPaused(bool bPaused);
	void TogglePause();
	void Surrender();
	float GetElapsedSeconds() const { return Elapsed; }
	const FString& GetMessage() const { return Message; }
	const FRTSMatchStats& GetStats(uint8 Team) const { return Stats[FMath::Min<int32>(Team, 1)]; }
	ARTSBuilding* GetMainBase(uint8 Team) const;
	FVector GetBaseLocation(uint8 Team) const { return BaseLocations[FMath::Min<int32>(Team, 1)]; }
	ARTSEnemyDirector* GetEnemyDirector() const;
	int32 GetUnitCount(uint8 Team, bool bWorkers) const;
	void NotifyUnitTrained(uint8 Team);
	void NotifyBuildingCompleted(uint8 Team);
	void NotifyGathered(uint8 Team, bool bWood, int32 Amount);
	FSimpleMulticastDelegate OnMatchChanged;
	bool HasDebugBeenUsed() const { return bDebugUsed; }
	void MarkDebugUsed() { if (!bDebugUsed) { bDebugUsed = true; OnMatchChanged.Broadcast(); } }
private:
	bool FindStartingAreas(FVector& Player, FVector& Enemy, FString& Error) const;
	void ClearRuntimeActors();
	void Finish(ERTSMatchPhase Result);
	void ObserveUnits();
	UFUNCTION() void OnUnitDied(URTSHealthComponent* Health, AActor* Killer);
	ERTSMatchPhase Phase = ERTSMatchPhase::Sandbox;
	ERTSMatchPhase MenuReturnPhase = ERTSMatchPhase::Sandbox;
	bool bMenuHasReturnState = false;
	bool bMenuReturnPaused = false;
	ERTSDifficulty Difficulty = ERTSDifficulty::Normal;
	float Elapsed = 0;
	float ObserveTimer = 0;
	FString Message;
	FRTSMatchStats Stats[2];
	FVector BaseLocations[2] = {FVector::ZeroVector, FVector::ZeroVector};
	bool bHasStartingAreas = false;
	TWeakObjectPtr<ARTSBuilding> MainBases[2];
	TWeakObjectPtr<ARTSEnemyDirector> EnemyDirector;
	TSet<TWeakObjectPtr<ARTSUnit>> ObservedUnits;
	bool bDebugUsed = false;
};
