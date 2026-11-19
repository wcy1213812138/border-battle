// MiniRTS M4 - paid economy and orders-only computer opponent.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Match/RTSMatchSubsystem.h"
#include "RTSEnemyDirector.generated.h"

class ARTSUnit;
class ARTSBuilding;
class URTSBalanceSubsystem;
struct FRTSAIWorldState;

// Decision policy only: unit/building prices and combat stats remain in RTSBalance.csv.
struct FRTSAIDifficulty
{
	float DecisionInterval = 1.0f;
	int32 WorkerTarget = 6;
	int32 ArmyTarget = 12;
	int32 AttackSize = 6;
	float FirstAttackSeconds = 120.0f;
	float AttackInterval = 100.0f;
	float ArcherFraction = 0.4f;
};

UCLASS()
class ARTSEnemyDirector : public AActor
{
	GENERATED_BODY()
public:
	ARTSEnemyDirector();
	void Configure(ERTSDifficulty InDifficulty);
	const FString& GetIntent() const { return Intent; }
	static bool LoadDifficulty(ERTSDifficulty Difficulty, FRTSAIDifficulty& OutParameters, FString& Error);
	const FRTSAIDifficulty& GetParameters() const { return Parameters; }

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	void CollectWorld(FRTSAIWorldState& State) const;
	void RunEconomy(FRTSAIWorldState& State, URTSBalanceSubsystem& Balance, URTSMatchSubsystem& Match);
	void RunMilitary(const FRTSAIWorldState& State, URTSMatchSubsystem& Match);
	ARTSBuilding* TryBuild(FName Id, const FVector& Home, const FRTSAIWorldState& State, URTSBalanceSubsystem& Balance);

	FRTSAIDifficulty Parameters;
	FString Intent = TEXT("尚未配置电脑难度");
	bool bConfigured = false;
	float DecisionTimer = 0.0f;
	float NextAttackSeconds = 0.0f;
	float NextPlacementSeconds = 0.0f;
	TArray<TWeakObjectPtr<ARTSUnit>> Wave;
	TSet<TWeakObjectPtr<ARTSUnit>> Defenders;
	TMap<TWeakObjectPtr<ARTSUnit>, float> LastWorkAssignment;
};
