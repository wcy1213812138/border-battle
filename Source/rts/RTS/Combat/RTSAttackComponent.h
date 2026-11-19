// MiniRTS - melee/ranged attack stats, range checks and swing timing.
// The owner decides *when* to attack (order/AI); this component decides *whether it can*
// and applies damage at the impact frame.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSAttackComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRTSAttackStartedSignature, AActor*, Target);

UCLASS(ClassGroup = (RTS), meta = (BlueprintSpawnableComponent))
class URTSAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSAttackComponent();

	void SetStats(float InDamage, float InRange, float InInterval, float InHitDelay);
	void SetProjectileStats(bool bInRanged, float InSpeed);

	/** Hostile non-neutral teams and a living target with health. Source may be a corpse for in-flight shots. */
	static bool IsHostileTarget(const AActor* Source, const AActor* Target);
	bool HasLineOfSight(const AActor* Target) const;
	static FVector AimLocation(const AActor* Actor);

	/** True if Target is alive and within AttackRange (edge-to-edge). */
	UFUNCTION(BlueprintPure, Category = "RTS|Attack")
	bool IsInRange(const AActor* Target) const;

	UFUNCTION(BlueprintPure, Category = "RTS|Attack")
	bool IsReady() const;

	/** Swing in progress (between start and impact). */
	UFUNCTION(BlueprintPure, Category = "RTS|Attack")
	bool IsSwinging() const;

	/** Starts a swing if ready and in range. Damage lands after HitDelay. */
	UFUNCTION(BlueprintCallable, Category = "RTS|Attack")
	bool TryStartAttack(AActor* Target);

	/** Cancels a pending swing (e.g. on death or new order). */
	UFUNCTION(BlueprintCallable, Category = "RTS|Attack")
	void CancelAttack();

	/** Edge-to-edge 2D distance using each actor's simple collision cylinder. */
	static float EdgeDistance2D(const AActor* A, const AActor* B);

	float GetRange() const { return Range; }
	bool IsRanged() const { return bRanged; }

	UPROPERTY(BlueprintAssignable, Category = "RTS|Attack")
	FRTSAttackStartedSignature OnAttackStarted;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void ResolveHit();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Attack", meta = (ClampMin = 0))
	float Damage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Attack", meta = (ClampMin = 0, Units = "cm"))
	float Range = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Attack", meta = (ClampMin = 0.05, Units = "s"))
	float Interval = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Attack", meta = (ClampMin = 0, Units = "s"))
	float HitDelay = 0.4f;

	/** Extra tolerance so a target that steps back slightly mid-swing still gets hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Attack", meta = (ClampMin = 0, Units = "cm"))
	float HitRangeTolerance = 50.0f;

private:
	bool bRanged = false;
	float ProjectileSpeed = 1200.0f;
	TWeakObjectPtr<AActor> PendingTarget;
	FTimerHandle HitTimer;
	double LastAttackTime = -1.0e9;
};
