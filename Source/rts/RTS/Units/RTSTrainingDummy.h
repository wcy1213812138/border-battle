// MiniRTS - stationary target for testing combat. Topples over at 0 HP and respawns.
// Its health bar/number is drawn by ARTSHUD (screen space, always faces the camera).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "Core/RTSTypes.h"
#include "RTSTrainingDummy.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class URTSHealthComponent;

UCLASS()
class ARTSTrainingDummy : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSTrainingDummy();

	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamId); }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void HandleHealthChanged(URTSHealthComponent* HealthComp, float NewHealth, float Delta);

	UFUNCTION()
	void HandleDied(URTSHealthComponent* HealthComp, AActor* Killer);

	void EndHitFlash();
	void Respawn();

	UPROPERTY(VisibleAnywhere, Category = "RTS|Dummy")
	TObjectPtr<UCapsuleComponent> Capsule;

	/** Rotates around the base so the dummy falls like a post. */
	UPROPERTY(VisibleAnywhere, Category = "RTS|Dummy")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Dummy")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Dummy")
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS|Dummy")
	TObjectPtr<URTSHealthComponent> Health;

	UPROPERTY(EditAnywhere, Category = "RTS|Dummy", meta = (ClampMin = 1))
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Dummy")
	uint8 TeamId = RTSTeam::Enemy;

	UPROPERTY(EditAnywhere, Category = "RTS|Dummy", meta = (Units = "s"))
	float RespawnDelay = 4.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Dummy", meta = (Units = "s"))
	float FallDuration = 0.6f;

	UPROPERTY(EditAnywhere, Category = "RTS|Dummy")
	FLinearColor BodyColor = FLinearColor(0.45f, 0.28f, 0.12f);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	FTimerHandle FlashTimer;
	FTimerHandle RespawnTimer;
	float FallElapsed = 0.0f;
	float FallYaw = 0.0f;
};
