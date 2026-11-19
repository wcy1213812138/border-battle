// MiniRTS - hit points for anything that can be damaged (units, buildings, training dummies).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRTSHealthChangedSignature, URTSHealthComponent*, HealthComp, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRTSDiedSignature, URTSHealthComponent*, HealthComp, AActor*, Killer);

UCLASS(ClassGroup = (RTS), meta = (BlueprintSpawnableComponent))
class URTSHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSHealthComponent();

	/** Applies damage. Returns the amount actually removed (0 if already dead). */
	UFUNCTION(BlueprintCallable, Category = "RTS|Health")
	float ApplyDamage(float Amount, AActor* DamageCauser);

	/** Restores living targets only; returns actual healing, never overheals. */
	float ApplyHealing(float Amount);

	/** Sets max health and refills to full. */
	UFUNCTION(BlueprintCallable, Category = "RTS|Health")
	void SetMaxHealth(float NewMaxHealth);
	/** Permanent upgrades preserve injury percentage and never revive dead actors. */
	void SetMaxHealthPreservingRatio(float NewMaxHealth);
	/** Restores a validated living snapshot without creating combat events. */
	void RestoreHealthFraction(float Fraction)
	{
		if (!FMath::IsFinite(Fraction) || Fraction <= 0 || Fraction > 1 || IsDead()) return;
		Health = MaxHealth * Fraction; LastDamageSource.Reset(); LastDamageTime = -1000;
	}
	UFUNCTION(BlueprintCallable, Category = "RTS|Health")
	void ResetHealth();
	/** Configures optional out-of-combat regeneration. Zero rate disables it. */
	void SetRegeneration(float HealthPerSecond, float DelaySeconds);

	UFUNCTION(BlueprintPure, Category = "RTS|Health")
	float GetRegenerationRate() const { return RegenerationRate; }

	UFUNCTION(BlueprintPure, Category = "RTS|Health")
	float GetRegenerationDelay() const { return RegenerationDelay; }


	UFUNCTION(BlueprintPure, Category = "RTS|Health")
	bool IsDead() const { return Health <= 0.0f; }

	UFUNCTION(BlueprintPure, Category = "RTS|Health")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "RTS|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "RTS|Health")
	float GetHealthPercent() const { return MaxHealth > 0.0f ? Health / MaxHealth : 0.0f; }

	AActor* GetLastDamageSource() const { return LastDamageSource.Get(); }
	double GetLastDamageTime() const { return LastDamageTime; }

	UPROPERTY(BlueprintAssignable, Category = "RTS|Health")
	FRTSHealthChangedSignature OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "RTS|Health")
	FRTSDiedSignature OnDied;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Health", meta = (ClampMin = 1))
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RTS|Health")
	float Health = 100.0f;
private:
	void RegenerateHealth();
	FTimerHandle RegenerationTimer;
	float RegenerationRate = 0.0f;
	float RegenerationDelay = 0.0f;
	TWeakObjectPtr<AActor> LastDamageSource;
	double LastDamageTime = -1000;
};
