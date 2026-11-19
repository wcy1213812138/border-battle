// MiniRTS - data-driven unit definition.
// All art references and balance numbers for a unit type live here, so swapping
// the art pack or rebalancing never touches C++.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSUnitData.generated.h"

class USkeletalMesh;
class UAnimSequence;

UCLASS(BlueprintType)
class URTSUnitData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info")
	FText DisplayName;

	// ---- Visuals ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	TObjectPtr<USkeletalMesh> Mesh;

	/** Mesh transform relative to the capsule *bottom*, so the model faces +X with feet on the ground.
	 *  Toony Tiny models face +Y, hence the default -90 yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	FTransform MeshOffset = FTransform(FRotator(0.0, -90.0, 0.0));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals", meta = (Units = "cm"))
	float CapsuleRadius = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals", meta = (Units = "cm"))
	float CapsuleHalfHeight = 80.0f;

	// ---- Animations (single-node playback, no Anim Blueprint needed) ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> MoveAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> AttackAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> DeathAnim;

	// ---- Balance ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = 1))
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = 0, Units = "cm/s"))
	float MoveSpeed = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = 0))
	float AttackDamage = 10.0f;

	/** Edge-to-edge distance (capsule radii excluded). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = 0, Units = "cm"))
	float AttackRange = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = 0.05, Units = "s"))
	float AttackInterval = 1.2f;

	/** Time from swing start to damage being applied (matches the animation's impact frame). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance", meta = (ClampMin = 0, Units = "s"))
	float AttackHitDelay = 0.4f;

	// ---- Combat behaviour ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bAutoAcquireTargets = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = 0, Units = "cm"))
	float AcquisitionRadius = 800.0f;

	/** Automatic pursuit is limited around the location where combat began. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = 0, Units = "cm"))
	float AutoChaseLeash = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bRangedAttack = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = 100, Units = "cm/s"))
	float ProjectileSpeed = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Healing")
	bool bHealer = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Healing")
	float HealAmount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Healing")
	float HealRange = 450;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Healing")
	float HealInterval = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Healing")
	float HealCastDelay = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	FName BalanceId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	bool bWorker = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = 1))
	int32 CarryCapacity = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = 0.05))
	float GatherInterval = 0.35f;
};
